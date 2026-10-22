// Compression and archives in memory: compress/flate at every level
// (including HuffmanOnly and NoCompression) with Flush points and a preset
// dictionary, compress/gzip with header fields and multistream concatenation,
// compress/zlib with a dictionary, compress/lzw in both bit orders and
// widths, compress/bzip2 (decode only, so it is fed a stream from the
// reference bzip2 embedded as hex), and archive/zip and archive/tar written
// to and read from buffers with fixed modification times.
//
// Compressed sizes and CRCs are printed alongside the round-tripped data:
// the output of these encoders is a deterministic function of the input and
// level, and the encoders are long loops of hash-chain matching and bit
// packing, so any miscompile changes a size or a checksum.
package main

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"compress/bzip2"
	"compress/flate"
	"compress/gzip"
	"compress/lzw"
	"compress/zlib"
	"encoding/hex"
	"fmt"
	"hash/crc32"
	"io"
	"strings"
	"time"
)

func corpus() []byte {
	var b strings.Builder
	words := strings.Fields("the quick brown fox jumps over the lazy dog lorem ipsum dolor sit amet wasm native compiler go runtime")
	seed := uint32(1)
	for i := 0; i < 4000; i++ {
		seed = seed*1103515245 + 12345
		b.WriteString(words[(seed>>16)%uint32(len(words))])
		if i%13 == 12 {
			b.WriteString(".\n")
		} else {
			b.WriteByte(' ')
		}
	}
	for i := 0; i < 2000; i++ {
		b.WriteByte(byte(i * i % 251))
	}
	return []byte(b.String())
}

func main() {
	data := corpus()
	fmt.Println("input bytes", len(data), "crc", fmt.Sprintf("%08x", crc32.ChecksumIEEE(data)))

	// flate at every level.
	for _, level := range []int{flate.NoCompression, flate.BestSpeed, 2, 3, 4, 5, 6, 7, 8, flate.BestCompression, flate.HuffmanOnly, flate.DefaultCompression} {
		var buf bytes.Buffer
		w, err := flate.NewWriter(&buf, level)
		if err != nil {
			fmt.Println("level", level, "error", err)
			continue
		}
		w.Write(data[:len(data)/2])
		w.Flush()
		mid := buf.Len()
		w.Write(data[len(data)/2:])
		w.Close()
		back, err := io.ReadAll(flate.NewReader(bytes.NewReader(buf.Bytes())))
		fmt.Printf("flate level %2d: %6d bytes (flush at %6d) crc %08x roundtrip=%v %v\n", level, buf.Len(), mid, crc32.ChecksumIEEE(buf.Bytes()), bytes.Equal(back, data), err)
	}
	_, err := flate.NewWriter(io.Discard, 42)
	fmt.Println("bad level:", err)

	// Dictionary.
	dict := []byte("the quick brown fox jumps over the lazy dog")
	var dbuf bytes.Buffer
	dw, _ := flate.NewWriterDict(&dbuf, flate.BestCompression, dict)
	dw.Write(data[:500])
	dw.Close()
	dback, _ := io.ReadAll(flate.NewReaderDict(bytes.NewReader(dbuf.Bytes()), dict))
	fmt.Println("flate dict:", dbuf.Len(), "bytes, roundtrip", bytes.Equal(dback, data[:500]))
	_, err = io.ReadAll(flate.NewReader(bytes.NewReader(dbuf.Bytes())))
	fmt.Println("without dict:", err)
	_, err = io.ReadAll(flate.NewReader(bytes.NewReader([]byte{0xff, 0xff, 0xff})))
	fmt.Println("corrupt flate:", err)

	// gzip with header and multistream.
	var gbuf bytes.Buffer
	gw, _ := gzip.NewWriterLevel(&gbuf, gzip.BestCompression)
	gw.Name = "corpus.txt"
	gw.Comment = "test corpus"
	gw.ModTime = time.Date(2020, 1, 2, 3, 4, 5, 0, time.UTC)
	gw.Extra = []byte("xtra")
	gw.Write(data)
	gw.Close()
	first := gbuf.Len()
	gw2 := gzip.NewWriter(&gbuf)
	gw2.Write([]byte("second member\n"))
	gw2.Close()
	fmt.Printf("gzip: first member %d bytes, total %d, header % x\n", first, gbuf.Len(), gbuf.Bytes()[:10])
	gr, err := gzip.NewReader(bytes.NewReader(gbuf.Bytes()))
	fmt.Println("gzip header:", err, gr.Name, gr.Comment, gr.ModTime.UTC(), string(gr.Extra), gr.OS)
	all, err := io.ReadAll(gr)
	fmt.Println("gzip multistream read:", len(all), err, bytes.Equal(all[:len(data)], data), string(all[len(data):]))
	gr2, _ := gzip.NewReader(bytes.NewReader(gbuf.Bytes()))
	gr2.Multistream(false)
	one, _ := io.ReadAll(gr2)
	fmt.Println("gzip single member:", len(one))
	bad := append([]byte(nil), gbuf.Bytes()[:first]...)
	bad[first-5] ^= 0xff // corrupt the CRC
	_, err = io.ReadAll(must(gzip.NewReader(bytes.NewReader(bad))))
	fmt.Println("gzip corrupt crc:", err)
	_, err = gzip.NewReader(strings.NewReader("not gzip"))
	fmt.Println("gzip bad header:", err)

	// zlib.
	for _, lvl := range []int{zlib.BestSpeed, zlib.DefaultCompression, zlib.BestCompression} {
		var zb bytes.Buffer
		zw, _ := zlib.NewWriterLevelDict(&zb, lvl, dict)
		zw.Write(data)
		zw.Close()
		zr, err := zlib.NewReaderDict(bytes.NewReader(zb.Bytes()), dict)
		zback, err2 := io.ReadAll(zr)
		fmt.Printf("zlib level %d: %d bytes adler %x %v %v roundtrip=%v\n", lvl, zb.Len(), zb.Bytes()[zb.Len()-4:], err, err2, bytes.Equal(zback, data))
	}
	var zb bytes.Buffer
	zw := zlib.NewWriter(&zb)
	zw.Write([]byte("hello hello hello hello"))
	zw.Close()
	fmt.Printf("zlib small % x\n", zb.Bytes())

	// lzw.
	for _, order := range []lzw.Order{lzw.LSB, lzw.MSB} {
		for _, width := range []int{7, 8} {
			src := data
			if width == 7 {
				src = bytes.Map(func(r rune) rune { return r & 0x7f }, data[:20000])
			}
			var lb bytes.Buffer
			lw := lzw.NewWriter(&lb, order, width)
			lw.Write(src)
			lw.Close()
			lback, err := io.ReadAll(lzw.NewReader(bytes.NewReader(lb.Bytes()), order, width))
			fmt.Printf("lzw order=%d width=%d: %d -> %d bytes crc %08x roundtrip=%v %v\n", order, width, len(src), lb.Len(), crc32.ChecksumIEEE(lb.Bytes()), bytes.Equal(lback, src), err)
		}
	}

	// bzip2 decode of a fixed stream ("hello bzip2 world\n" x 3 compressed by bzip2 -9).
	bz, _ := hex.DecodeString("425a6839314159265359c06e699e00000bd9800010400010001664d090200022bfd544c8d91a840000ca194b4943e53ae356529c429f8bb9229c2848603734cf00")
	out, err := io.ReadAll(bzip2.NewReader(bytes.NewReader(bz)))
	fmt.Printf("bzip2 decoded %q %v\n", out, err)
	_, err = io.ReadAll(bzip2.NewReader(strings.NewReader("BZh9garbage")))
	fmt.Println("bzip2 garbage:", err)

	// zip archive.
	var zbuf bytes.Buffer
	zwr := zip.NewWriter(&zbuf)
	mod := time.Date(2021, 6, 7, 8, 9, 10, 0, time.UTC)
	files := []struct {
		name   string
		body   []byte
		method uint16
	}{{"readme.txt", []byte("hello zip\n"), zip.Store}, {"dir/data.bin", data, zip.Deflate}, {"dir/empty", nil, zip.Deflate}}
	for _, f := range files {
		h := &zip.FileHeader{Name: f.name, Method: f.method, Modified: mod, Comment: "c:" + f.name}
		w, err := zwr.CreateHeader(h)
		if err != nil {
			fmt.Println("zip create:", err)
			continue
		}
		w.Write(f.body)
	}
	zwr.SetComment("archive comment")
	zwr.Close()
	fmt.Println("zip size", zbuf.Len(), "crc", fmt.Sprintf("%08x", crc32.ChecksumIEEE(zbuf.Bytes())))
	zr, err := zip.NewReader(bytes.NewReader(zbuf.Bytes()), int64(zbuf.Len()))
	fmt.Println("zip open:", err, "comment", zr.Comment, "files", len(zr.File))
	for _, f := range zr.File {
		rc, err := f.Open()
		body, _ := io.ReadAll(rc)
		rc.Close()
		fmt.Printf("  %-12s method=%d size=%d comp=%d crc=%08x mod=%s ok=%v %v comment=%q\n", f.Name, f.Method, f.UncompressedSize64, f.CompressedSize64, f.CRC32, f.Modified.UTC().Format(time.RFC3339), crc32.ChecksumIEEE(body) == f.CRC32, err, f.Comment)
	}
	fsf, err := zr.Open("dir/data.bin")
	if err == nil {
		st, _ := fsf.Stat()
		fmt.Println("zip fs.File stat", st.Name(), st.Size(), st.IsDir())
		fsf.Close()
	}

	// tar archive.
	var tbuf bytes.Buffer
	tw := tar.NewWriter(&tbuf)
	entries := []struct {
		name string
		body string
		typ  byte
		mode int64
	}{{"a.txt", "alpha\n", tar.TypeReg, 0644}, {"sub/", "", tar.TypeDir, 0755}, {"sub/b.txt", strings.Repeat("beta ", 300), tar.TypeReg, 0600}, {"link", "", tar.TypeSymlink, 0777}}
	for _, e := range entries {
		hdr := &tar.Header{Name: e.name, Mode: e.mode, Size: int64(len(e.body)), Typeflag: e.typ, ModTime: mod, Uname: "gopher", Gname: "wasm", Format: tar.FormatPAX}
		if e.typ == tar.TypeSymlink {
			hdr.Linkname = "a.txt"
		}
		if err := tw.WriteHeader(hdr); err != nil {
			fmt.Println("tar header:", err)
		}
		tw.Write([]byte(e.body))
	}
	tw.Close()
	fmt.Println("tar size", tbuf.Len(), "crc", fmt.Sprintf("%08x", crc32.ChecksumIEEE(tbuf.Bytes())))
	tr := tar.NewReader(&tbuf)
	for {
		h, err := tr.Next()
		if err == io.EOF {
			fmt.Println("tar end")
			break
		}
		if err != nil {
			fmt.Println("tar err", err)
			break
		}
		body, _ := io.ReadAll(tr)
		fmt.Printf("  %-10s type=%c mode=%o size=%d link=%q uname=%s mod=%s body=%d crc=%08x\n", h.Name, h.Typeflag, h.Mode, h.Size, h.Linkname, h.Uname, h.ModTime.UTC().Format(time.RFC3339), len(body), crc32.ChecksumIEEE(body))
	}
}

func must[T any](v T, err error) T {
	if err != nil {
		panic(err)
	}
	return v
}
