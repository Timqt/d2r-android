// 存档导入/导出的回归：压缩-解压往返、说明文件、越界路径、空包、原子替换与回滚。
// Save import/export regression: zip/unzip round trip, the marker entry, path escapes, empty archives, the
// atomic swap and its rollback. Plain Java so it runs on the host (no Android APIs involved).
package org.d2r.android.port;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class SaveArchiveTest {
    private static int checks;
    private static void check(boolean condition, String message) {
        checks++;
        if (!condition) throw new AssertionError(message);
        System.out.println("  ✓ " + message);
    }

    private static File tempDir(String name) throws IOException {
        File dir = Files.createTempDirectory("d2r-save-" + name).toFile();
        dir.deleteOnExit();
        return dir;
    }

    private static void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("mkdirs " + parent);
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    private interface IoAction { void run() throws IOException; }

    private static boolean fails(IoAction action) {
        try { action.run(); return false; } catch (IOException expected) { return true; }
    }

    public static void main(String[] args) throws Exception {
        // 造一棵带子目录 / 空文件 / 中文名 / 二进制内容的存档树
        File save = tempDir("save");
        write(new File(save, "AndroidProbe/character.dat"), "hero-data");
        write(new File(save, "AndroidProbe/sub/深层 数据.bin"), "\u0000\u0001binaryÿ");
        write(new File(save, "Settings/settings.json"), "{\"volume\":1}");
        write(new File(save, "empty.txt"), "");

        ByteArrayOutputStream exported = new ByteArrayOutputStream();
        int written = SaveArchive.zip(save, exported, 1700000000000L);
        check(written == 4, "导出写入 4 个文件（实际 " + written + "）");
        byte[] archive = exported.toByteArray();
        check(archive.length > 0, "导出的 zip 非空（" + archive.length + " 字节）");

        File restored = tempDir("restored");
        int extracted = SaveArchive.unzip(new ByteArrayInputStream(archive), restored);
        check(extracted == 4, "解压恢复 4 个文件（实际 " + extracted + "）");
        check(read(new File(restored, "AndroidProbe/character.dat")).equals("hero-data"), "一级子目录内容一致");
        check(read(new File(restored, "AndroidProbe/sub/深层 数据.bin")).contains("binary"), "深层子目录 + 中文名内容一致");
        check(read(new File(restored, "Settings/settings.json")).equals("{\"volume\":1}"), "并列目录内容一致");
        check(new File(restored, "empty.txt").isFile() && new File(restored, "empty.txt").length() == 0, "空文件也恢复");
        check(!new File(restored, SaveArchive.MARKER).exists(), "说明文件不会被当成存档解出来");

        // 越界路径：`..`、绝对路径、反斜杠都必须拒绝
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(zipWith("../evil.txt")), tempDir("neg1"))), "拒绝包含 .. 的越界路径");
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(zipWith("/absolute.txt")), tempDir("neg2"))), "拒绝绝对路径");
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(zipWith("dir\\evil.txt")), tempDir("neg3"))), "拒绝反斜杠路径");
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(zipWith(SaveArchive.MARKER)), tempDir("neg4"))), "只有说明文件的包被拒绝（没有存档）");
        // 空包也要拒绝：解出一个空存档比报错更糟
        ByteArrayOutputStream emptyZip = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(emptyZip)) { zip.finish(); }
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(emptyZip.toByteArray()), tempDir("neg5"))), "空包被拒绝");

        // 替换：旧存档整份换成导入的，不留备份
        File parent = tempDir("swap");
        File live = new File(parent, "save");
        write(new File(live, "AndroidProbe/character.dat"), "old");
        File imported = new File(parent, "save.import");
        write(new File(imported, "AndroidProbe/character.dat"), "new");
        SaveArchive.replace(live, imported);
        check(read(new File(live, "AndroidProbe/character.dat")).equals("new"), "替换后是导入的存档");
        check(!new File(parent, "save.replaced").exists(), "替换成功不留备份目录");
        check(!imported.exists(), "导入用的临时目录已被挪走");

        // 替换失败要回滚：导入目录不存在时，原存档必须还在
        File failParent = tempDir("rollback");
        File failLive = new File(failParent, "save");
        write(new File(failLive, "AndroidProbe/character.dat"), "keep-me");
        File missing = new File(failParent, "save.import");
        check(fails(() -> SaveArchive.replace(failLive, missing)), "导入目录缺失时替换报错");
        check(read(new File(failLive, "AndroidProbe/character.dat")).equals("keep-me"), "替换失败后原存档完好");

        // 原来没有存档时，导入直接就位
        File freshParent = tempDir("fresh");
        File freshLive = new File(freshParent, "save");
        File freshImported = new File(freshParent, "save.import");
        write(new File(freshImported, "AndroidProbe/character.dat"), "first");
        SaveArchive.replace(freshLive, freshImported);
        check(read(new File(freshLive, "AndroidProbe/character.dat")).equals("first"), "首次导入（原本没有存档）就位");

        System.out.println("== " + checks + " 项全部通过 ==");
    }

    // 构造只含一条记录的最小 zip（只用于越界路径这类负例）。
    private static byte[] zipWith(String entryName) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            zip.putNextEntry(new ZipEntry(entryName));
            zip.write("x".getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
        } catch (IOException error) {
            throw new IllegalStateException(error);
        }
        return out.toByteArray();
    }

    private static void unzipQuietly(byte[] archive) {
        try {
            SaveArchive.unzip(new ByteArrayInputStream(archive), tempDir("negative"));
        } catch (IOException error) {
            throw new RuntimeException(error);
        }
    }
}
