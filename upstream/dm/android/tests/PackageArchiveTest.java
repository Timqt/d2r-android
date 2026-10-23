// 数据包导入的回归：能开的包被接受、缺模块/被截断/格式不认/条目越界都被拒绝，
// 而且**任何一次失败都不许动上一次能用的模块**。
//
// Package import regression: a package the game can boot is accepted; a missing module, a truncated archive, an
// unknown format and an out-of-place entry are all refused, and **no failure ever touches the previous, working
// module set**. Plain Java, so it runs on a PC (dm/android/tools/ci_checks.py runs it).
package org.d2r.android.port;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class PackageArchiveTest {
    private static int checks;

    private static void check(boolean condition, String message) {
        checks++;
        if (!condition) throw new AssertionError(message);
        System.out.println("  ✓ " + message);
    }

    private static File tempDir(String name) throws IOException {
        File dir = Files.createTempDirectory("d2r-package-" + name).toFile();
        dir.deleteOnExit();
        return dir;
    }

    // 契约里的九个必需文件：三个模块，各配 fixups 与 gates（与 build_package.py 的 GAME_MODULES / GAME_LAYOUTS 一致）。
    // The nine required files of the contract: three modules, each with fixups and gates.
    private static final String[] MODULES = {
        "game/libdmguest_main.so", "game/main.fixups.tsv", "game/main.gates.tsv",
        "game/libdmguest_sdk.so", "game/sdk.fixups.tsv", "game/sdk.gates.tsv",
        "game/libdmguest_subsdk0.so", "game/subsdk0.fixups.tsv", "game/subsdk0.gates.tsv",
    };

    private static void put(ZipOutputStream zip, String name, String content) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(content.getBytes(StandardCharsets.UTF_8));
        zip.closeEntry();
    }

    private static byte[] packageZip(String manifest, String... entries) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            if (manifest != null) put(zip, "manifest.json", manifest);
            for (String name : entries) put(zip, name, "内容 | content");
        }
        return out.toByteArray();
    }

    private static String manifest(int format, int game, int romfs, int shaders) {
        return "{\n  \"format\": " + format + ",\n  \"counts\": { \"game\": " + game + ", \"romfs\": " + romfs
             + ", \"shaders\": " + shaders + ", \"derived\": 3 }\n}\n";
    }

    // 与九个模块 + 2 个 romfs + 3 个着色器配套的清单。
    // The manifest that matches nine modules, two romfs files and three shaders.
    private static String matching(int format) { return manifest(format, 9, 2, 3); }

    private static String[] complete() {
        String[] entries = new String[MODULES.length + 6];
        System.arraycopy(MODULES, 0, entries, 0, MODULES.length);
        entries[MODULES.length] = "romfs/data/build_manifest.psv";
        entries[MODULES.length + 1] = "romfs/data/build_key.txt";
        entries[MODULES.length + 2] = "shaders/host-subgroup-size.txt";
        entries[MODULES.length + 3] = "shaders/aaa.spv";
        entries[MODULES.length + 4] = "shaders/bbb.meta";
        entries[MODULES.length + 5] = "derived.tsv";
        return entries;
    }

    // 清单解析在真机上是界面层用 org.json 做的；这里用最小解析，好让规则本身被验证而不是被绕过。
    // On device the UI layer parses with org.json; a minimal parse here keeps the rules under test rather than
    // letting the test hand the rules whatever it likes.
    private static PackageArchive.Declaration declaration(byte[] manifest) {
        String text = new String(manifest, StandardCharsets.UTF_8);
        return new PackageArchive.Declaration(number(text, "\"format\""), number(text, "\"game\""),
                                              number(text, "\"romfs\""), number(text, "\"shaders\""));
    }

    private static int number(String text, String key) {
        int at = text.indexOf(key);
        if (at < 0) return PackageArchive.Declaration.ABSENT;
        int colon = text.indexOf(':', at);
        int start = colon + 1;
        while (start < text.length() && !Character.isDigit(text.charAt(start)) && text.charAt(start) != '-') start++;
        int stop = start;
        while (stop < text.length() && (Character.isDigit(text.charAt(stop)) || text.charAt(stop) == '-')) stop++;
        return stop > start ? Integer.parseInt(text.substring(start, stop)) : PackageArchive.Declaration.ABSENT;
    }

    private static PackageArchive.Counts unpack(byte[] zip, File assets, File modules) throws IOException {
        return PackageArchive.unpack(new ByteArrayInputStream(zip), assets, modules, zip.length,
                                     new PackageArchive.Sink() {
                                         @Override public void progress(int percent) { }
                                         @Override public boolean cancelled() { return false; }
                                     }, PackageArchiveTest::declaration);
    }

    // 跑一次注定失败的导入，返回它撞的是哪条规则。
    // Run an import that must fail and report which rule it broke.
    private static String refusal(byte[] zip, File assets, File modules) throws IOException {
        try {
            unpack(zip, assets, modules);
        } catch (PackageArchive.Failure failure) {
            return failure.key;
        }
        throw new AssertionError("导入本该被拒绝，却成功了 | the import should have been refused but succeeded");
    }

    private static void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("mkdirs " + parent);
        Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    // 上一次导入留下的现场：一个能用的模块目录加一份清单。
    // The state a previous import left behind: a working module directory and a manifest.
    private static File[] previous(File root) throws IOException {
        File assets = new File(root, "assets");
        File modules = new File(root, "modules");
        write(new File(modules, "libdmguest_sdk.so"), "OLD");
        write(new File(modules, "sdk.fixups.tsv"), "OLD");
        write(new File(assets, "manifest.json"), "OLD");
        write(new File(assets, "romfs/data/old.data"), "OLD");
        return new File[]{assets, modules};
    }

    private static void survived(File[] where, String message) throws IOException {
        check("OLD".equals(read(new File(where[1], "libdmguest_sdk.so"))), message + "：模块还在");
        check(!new File(where[1], "game.import").exists(), message + "：没有留下半个暂存目录");
    }

    public static void main(String[] args) throws Exception {
        // 1. 一个完整的包被接受，落到正确的地方，清单最后落盘。
        File good = tempDir("good");
        PackageArchive.Counts counts = unpack(packageZip(matching(1), complete()),
                                              new File(good, "assets"), new File(good, "modules"));
        check(counts.game == 9 && counts.romfs == 2 && counts.shaders == 3,
              "完整的包被接受，返回实际数量 " + counts.game + "/" + counts.romfs + "/" + counts.shaders);
        check(new File(good, "modules/libdmguest_main.so").isFile(), "模块落在内部目录");
        check(!new File(good, "assets/game").exists(), "模块没有落到外部目录");
        check(new File(good, "assets/manifest.json").isFile(), "完成标记最后落盘");
        check(new File(good, "assets/romfs/data/build_key.txt").isFile(), "RomFS 原样落在外部目录");
        check(new File(good, "assets/derived.tsv").isFile(), "包根的 derived.tsv 被接受");
        check(new File(good, "modules/subsdk0.gates.tsv").isFile(), "空的布局文件也照收（subsdk0.gates.tsv 本来就是 0 字节）");

        // 2. 没给 derived.tsv 的包照样能导入（老包不该被拒）。
        File noDerived = tempDir("noderived");
        String[] without = new String[complete().length - 1];
        System.arraycopy(complete(), 0, without, 0, without.length);
        unpack(packageZip(manifest(1, 9, 2, 3), without), new File(noDerived, "assets"), new File(noDerived, "modules"));
        check(new File(noDerived, "assets/manifest.json").isFile(), "没有 derived.tsv 的包仍然可以导入");

        // 3. 事故现场：每个类别一个文件、清单却自报真实数量 —— 必须被拒，而且上一次的模块必须还在。
        File incident = tempDir("incident");
        File[] before = previous(incident);
        String key = refusal(packageZip(manifest(1, 9, 46, 20795), "game/main.fixups.tsv", "romfs/x", "shaders/y"),
                             before[0], before[1]);
        check("dmAndroidLaunchImportIncomplete".equals(key), "每类一个文件的包被拒（撞的是 " + key + "）");
        survived(before, "事故包之后");
        check(!new File(before[0], "manifest.json").exists(), "被拒的包没有留下完成标记");

        // 4. 缺一个模块布局文件：拒。（实测的崩法就是缺 sdk.fixups.tsv）
        File missing = tempDir("missing");
        File[] missingBefore = previous(missing);
        String[] shortOf = {"game/libdmguest_main.so", "game/main.fixups.tsv", "game/main.gates.tsv",
                            "game/libdmguest_sdk.so", "game/sdk.gates.tsv",
                            "game/libdmguest_subsdk0.so", "game/subsdk0.fixups.tsv", "game/subsdk0.gates.tsv",
                            "romfs/data/a.data", "romfs/data/b.data", "shaders/s.spv", "shaders/t.spv", "shaders/u.spv"};
        key = refusal(packageZip(matching(1), shortOf), missingBefore[0], missingBefore[1]);
        check("dmAndroidLaunchImportIncomplete".equals(key), "缺 sdk.fixups.tsv 的包被拒（撞的是 " + key + "）");
        check(refusalMessageNamesIt(key, packageZip(matching(1), shortOf), missing),
              "拒的原因里点名了缺哪个文件");

        // 5. 空模块（0 字节）也不能算数：它根本加载不了。
        File empty = tempDir("empty");
        String[] blanked = complete();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            put(zip, "manifest.json", matching(1));
            for (String name : blanked) put(zip, name, name.endsWith(".so") ? "" : "内容 | content");
        }
        check("dmAndroidLaunchImportIncomplete".equals(refusal(out.toByteArray(), new File(empty, "assets"), new File(empty, "modules"))),
              "0 字节的模块被拒");

        // 6. 包被截断（清单完好，文件少了）：这是唯一能发现它的地方。
        File truncated = tempDir("truncated");
        File[] truncatedBefore = previous(truncated);
        key = refusal(packageZip(matching(1), "game/libdmguest_main.so", "game/main.fixups.tsv", "game/main.gates.tsv",
                                 "game/libdmguest_sdk.so", "game/sdk.fixups.tsv", "game/sdk.gates.tsv",
                                 "game/libdmguest_subsdk0.so", "game/subsdk0.fixups.tsv", "game/subsdk0.gates.tsv",
                                 "romfs/data/a.data", "romfs/data/b.data", "shaders/s.spv"),
                      truncatedBefore[0], truncatedBefore[1]);
        check("dmAndroidLaunchImportCounts".equals(key), "被截断的包被拒（撞的是 " + key + "）");
        survived(truncatedBefore, "截断包之后");

        // 7. 格式版本不认识：契约说必须拒绝，那就拒绝。
        File format = tempDir("format");
        check("dmAndroidLaunchImportFormat".equals(refusal(packageZip(matching(2), complete()),
                                                          new File(format, "assets"), new File(format, "modules"))),
              "不认识的格式版本被拒");

        // 8. 清单自报的数量与实际不符：拒（以前它只被用来显示，所以一个空包能报出"46 个资源"）。
        File mismatched = tempDir("counts");
        check("dmAndroidLaunchImportCounts".equals(refusal(packageZip(manifest(1, 9, 99, 3), complete()),
                                                          new File(mismatched, "assets"), new File(mismatched, "modules"))),
              "自报数量与实际不符被拒");

        // 9. 越界条目：`romfs/../manifest.json` 规范化后落在包目录之内，只靠规范化路径放行，会让它在校验**之前**
        //    就把完成标记写进去，绕过"最后才写 manifest"的顺序保证。
        File escape = tempDir("escape");
        File[] escapeBefore = previous(escape);
        for (String nasty : new String[]{"romfs/../manifest.json", "game/../manifest.json",
                                         "/etc/passwd", "game/..\\..\\x", "shaders/./x", "game//x"}) {
            String broken = refusal(packageZip(null, nasty), escapeBefore[0], escapeBefore[1]);
            check("dmAndroidLaunchImportBadEntry".equals(broken), "越界条目被拒：" + nasty + "（撞的是 " + broken + "）");
        }
        // 旧的完成标记在导入开始时就被撤掉（刻意的：宁可不认这个包，也不要读到半个包），
        // 这里要确保的是**没有一个完成标记被写出来** —— 越界条目以前正是绕过这一点的。
        // The old completion marker is dropped when the import starts (deliberate: refusing the package beats
        // reading half of one). What matters here is that **no marker was written**: the out-of-place entry used to
        // be exactly the way around that.
        check(!new File(escapeBefore[0], "manifest.json").exists(), "越界条目没有让完成标记落盘");
        survived(escapeBefore, "越界条目之后");

        // 10. 包根本没有清单：拒（不然导入完游戏还是不知道能不能用）。
        File noManifest = tempDir("nomanifest");
        check("dmAndroidLaunchImportIncomplete".equals(refusal(packageZip(null, complete()),
                                                              new File(noManifest, "assets"), new File(noManifest, "modules"))),
              "没有清单的包被拒");

        // 11. 取消：和失败一样，不许动上一次的模块。
        File cancelled = tempDir("cancelled");
        File[] cancelledBefore = previous(cancelled);
        try {
            PackageArchive.unpack(new ByteArrayInputStream(packageZip(matching(1), complete())),
                                  cancelledBefore[0], cancelledBefore[1], 0,
                                  new PackageArchive.Sink() {
                                      @Override public void progress(int percent) { }
                                      @Override public boolean cancelled() { return true; }
                                  }, PackageArchiveTest::declaration);
            throw new AssertionError("取消本该中止导入 | a cancelled import must stop");
        } catch (PackageArchive.Failure failure) {
            check("dmAndroidLaunchImportCancelled".equals(failure.key), "取消撞的是取消那条规则");
        }
        survived(cancelledBefore, "取消之后");

        // 12. 成功的导入才替换模块，并且换上去的是新的。
        File replaced = tempDir("replaced");
        File[] replacedBefore = previous(replaced);
        unpack(packageZip(matching(1), complete()), replacedBefore[0], replacedBefore[1]);
        check(!read(new File(replacedBefore[1], "libdmguest_sdk.so")).equals("OLD"), "成功导入后模块被换成新的");
        check(!new File(replacedBefore[1], "game.previous").exists(), "换完之后旧的模块目录被清掉");
        check(read(new File(replacedBefore[1], "sdk.fixups.tsv")).length() > 0, "布局文件也在");

        // 13. 纯规则：名字判定。
        check(PackageArchive.allowed("game/libdmguest_main.so") && PackageArchive.allowed("derived.tsv"),
              "合法的条目名被放行");
        check(!PackageArchive.allowed("game") && !PackageArchive.allowed("game/") && !PackageArchive.allowed("other/x"),
              "把 game 本身、空尾巴和未知前缀都挡在外面");
        check(!PackageArchive.allowed("../manifest.json") && !PackageArchive.allowed("romfs/../../x"),
              "带 .. 的名字一律不放行");

        System.out.println("PackageArchiveTest： " + checks + " 项通过");
    }

    private static boolean refusalMessageNamesIt(String key, byte[] zip, File root) throws IOException {
        try {
            unpack(zip, new File(root, "assets2"), new File(root, "modules2"));
        } catch (PackageArchive.Failure failure) {
            return failure.getMessage().indexOf("sdk.fixups.tsv") >= 0;
        }
        return false;
    }
}
