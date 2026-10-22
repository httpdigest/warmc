// Web-facing text handling that needs no network: html/template's contextual
// auto-escaping (element text, attributes, URLs, JavaScript strings and
// values, CSS, and the typed template.HTML/JS/URL exemptions), html.Escape/
// UnescapeString with named and numeric entities, net/url parsing of
// absolute, relative, opaque and IPv6-host URLs with userinfo, query and
// fragment, ResolveReference, Query/Values encoding (sorted by key),
// PathEscape vs QueryEscape, JoinPath; path and path/filepath Clean, Join,
// Split, Ext, Base, Dir, Match, Rel; mime.ParseMediaType and FormatMediaType;
// net/netip addresses and prefixes; and net/mail address lists.
//
// html/template runs a context-tracking escaper over the parse tree before
// the first execution -- a state machine with dozens of states -- so the same
// value prints differently depending on where it lands.
package main

import (
	"fmt"
	"html"
	"html/template"
	"mime"
	"net/mail"
	"net/netip"
	"net/url"
	"path"
	"path/filepath"
	"strings"
)

func main() {
	// html/template contexts.
	page := `<html><head><title>{{.Title}}</title>
<style>p { color: {{.Color}}; } .c{background:url({{.URL}})}</style>
<script>var name = {{.Name}}; var n = {{.N}}; var obj = {{.Obj}}; var s = "{{.Name}}";</script></head>
<body class="{{.Class}}" onclick="go({{.Name}})">
<a href="{{.URL}}">link</a> <a href="/search?q={{.Query}}&x=1">search</a> <a href="{{.Evil}}">evil</a>
<p title='{{.Title}}'>{{.Body}}</p>{{.Trusted}} <a href="{{.TrustedURL}}">ok</a>
<!-- comments are stripped {{.Title}} -->
{{range .List}}<li data-v="{{.}}">{{.}}</li>{{end}}
</body></html>`
	t := template.Must(template.New("page").Parse(page))
	data := map[string]any{
		"Title":      `Tom & Jerry's "Show" <live>`,
		"Color":      "red; background: url(evil)",
		"URL":        "http://example.com/a b?c=d&e=<f>",
		"Name":       `O'Reilly </script><script>alert(1)</script>`,
		"N":          42.5,
		"Obj":        map[string]any{"k": []int{1, 2}, "s": "</script>"},
		"Class":      `a" onmouseover="x`,
		"Query":      "go & wasm/é",
		"Evil":       "javascript:alert(1)",
		"Body":       "<b>bold?</b> & more",
		"Trusted":    template.HTML("<i>trusted italic</i>"),
		"TrustedURL": template.URL("javascript:void(0)"),
		"List":       []string{"<a>", `"q"`, "plain"},
	}
	var sb strings.Builder
	err := t.Execute(&sb, data)
	fmt.Println(sb.String())
	fmt.Println("execute err:", err)

	// html escaping.
	for _, s := range []string{`<a href="x">Tom & 'Jerry'</a>`, "plain", "éè \U0001F600"} {
		fmt.Printf("EscapeString %q -> %q\n", s, html.EscapeString(s))
	}
	for _, s := range []string{"&lt;b&gt;", "&amp;amp;", "&eacute;&Eacute;&#233;&#xE9;&#XE9;", "&notit; &notin;", "&nbsp;|&copy;|&#0;|&#x110000;", "&unknown; & &;", "&lt", "&#65;&#66"} {
		fmt.Printf("UnescapeString %q -> %q\n", s, html.UnescapeString(s))
	}
	fmt.Println("template.HTMLEscapeString", template.HTMLEscapeString(`<"'&>`), "JSEscapeString", template.JSEscapeString(`'"<>&=\`+"\n"), "URLQueryEscaper", template.URLQueryEscaper("a b", "&"))

	// URLs.
	for _, raw := range []string{
		"https://user:p%40ss@Example.COM:8443/a/b%20c/../d;p?x=1&y=2&x=3#frag%21",
		"http://[2001:db8::1]:80/path",
		"mailto:someone@example.com",
		"/relative/path?q=go+lang#top",
		"//host.only/x",
		"file:///etc/hosts",
		"HTTP://example.com/%7Euser",
		"http://example.com/a?%zz",
		"http://a b.com/",
		"http://example.com/\x7f",
		"?just=query",
	} {
		u, err := url.Parse(raw)
		if err != nil {
			fmt.Printf("Parse(%q) error: %v\n", raw, err)
			continue
		}
		pw, hasPw := u.User.Password()
		fmt.Printf("Parse(%q)\n  scheme=%q opaque=%q user=%q pw=%q(%v) host=%q hostname=%q port=%q path=%q rawpath=%q query=%q frag=%q abs=%v\n  String=%q EscapedPath=%q RequestURI=%q\n",
			raw, u.Scheme, u.Opaque, u.User.Username(), pw, hasPw, u.Host, u.Hostname(), u.Port(), u.Path, u.RawPath, u.RawQuery, u.Fragment, u.IsAbs(),
			u.String(), u.EscapedPath(), u.RequestURI())
	}
	base, _ := url.Parse("http://a/b/c/d;p?q")
	for _, ref := range []string{"g", "./g", "g/", "/g", "//g", "?y", "g?y", "#s", "g#s", "..", "../g", "../..", "../../g", "../../../g", ".", "g;x", ""} {
		r, _ := url.Parse(ref)
		fmt.Printf("resolve %-10q -> %s\n", ref, base.ResolveReference(r))
	}
	vals := url.Values{}
	vals.Set("name", "Go Gopher")
	vals.Add("tag", "a&b")
	vals.Add("tag", "c=d")
	vals.Set("empty", "")
	vals.Set("unicode", "héllo wörld")
	enc := vals.Encode()
	fmt.Println("Encode", enc)
	parsed, err := url.ParseQuery(enc + "&bad=%zz")
	fmt.Println("ParseQuery tag", parsed["tag"], "name", parsed.Get("name"), "has empty", parsed.Has("empty"), "err", err)
	pq, err := url.ParseQuery("a=1;b=2")
	fmt.Println("semicolon query", pq, err)
	for _, s := range []string{"a b/c?d=e&f#g", "é/~-._", "100%"} {
		fmt.Printf("escape %q: Path=%q Query=%q\n", s, url.PathEscape(s), url.QueryEscape(s))
	}
	for _, s := range []string{"a%20b+c", "%e9", "%zz", "%"} {
		p, e1 := url.PathUnescape(s)
		q, e2 := url.QueryUnescape(s)
		fmt.Printf("unescape %q: path=%q %v query=%q %v\n", s, p, e1, q, e2)
	}
	jp, err := url.JoinPath("https://example.com/base/", "../a", "b c", "/d/")
	fmt.Println("JoinPath", jp, err)
	u2 := &url.URL{Scheme: "https", Host: "example.com", Path: "/a b/ü", RawQuery: url.Values{"z": {"1"}, "a": {"2"}}.Encode(), Fragment: "f g"}
	fmt.Println("built URL", u2.String(), u2.Redacted())
	u3, _ := url.Parse("https://alice:secret@example.com/")
	fmt.Println("Redacted", u3.Redacted())

	// path and filepath.
	for _, p := range []string{"", ".", "/", "a/b/../c", "/../a//b/./c/", "a/../../b", "//x", "a/b/c.tar.gz", "noext", ".hidden", "dir/"} {
		dir, file := path.Split(p)
		fmt.Printf("path %-18q Clean=%-10q Base=%-10q Dir=%-8q Ext=%-6q Split=(%q,%q) IsAbs=%v\n", p, path.Clean(p), path.Base(p), path.Dir(p), path.Ext(p), dir, file, path.IsAbs(p))
	}
	fmt.Println("Join", path.Join("a", "", "/b", "../c", "d/"), path.Join(), path.Join("", ""))
	for _, m := range [][2]string{{"*.go", "main.go"}, {"*.go", "dir/main.go"}, {"a/*/c", "a/b/c"}, {"[a-c]?", "bx"}, {"[^a-c]?", "bx"}, {`\*`, "*"}, {"[", "a"}, {"a*b*c", "axxbyyc"}} {
		ok, err := path.Match(m[0], m[1])
		fmt.Printf("Match(%q, %q) = %v %v\n", m[0], m[1], ok, err)
	}
	fmt.Println("filepath", filepath.Join("x", "y", "..", "z"), filepath.Clean("/a/./b/../../c"), filepath.Ext("f.txt"), filepath.ToSlash("a/b"), filepath.IsLocal("a/b"), filepath.IsLocal("../a"), filepath.IsLocal("/abs"))
	for _, rp := range [][2]string{{"/a/b", "/a/b/c/d"}, {"/a/b/c", "/a/x"}, {"a", "b"}, {"/a", "b"}} {
		r, err := filepath.Rel(rp[0], rp[1])
		fmt.Printf("Rel(%q,%q) = %q %v\n", rp[0], rp[1], r, err)
	}
	fmt.Printf("SplitList %q VolumeName %q\n", filepath.SplitList("/bin:/usr/bin::/x"), filepath.VolumeName("/a/b"))

	// mime.
	for _, s := range []string{
		`text/html; charset=UTF-8`, `multipart/form-data; boundary="----abc"; charset=utf-8`,
		`application/json`, `TEXT/Plain ; Format=Flowed`, `attachment; filename*=UTF-8''%e2%82%ac%20rates.txt`,
		`text/plain; charset=`, `bad/`, `a/b; c="unterminated`,
	} {
		mt, params, err := mime.ParseMediaType(s)
		var ks []string
		for k, v := range params {
			ks = append(ks, k+"="+v)
		}
		sortStrings(ks)
		fmt.Printf("ParseMediaType(%q) = %q %v %v\n", s, mt, ks, err)
	}
	fmt.Println("FormatMediaType", mime.FormatMediaType("text/plain", map[string]string{"charset": "utf-8", "name": "a b.txt", "z": "1"}),
		mime.FormatMediaType("attachment", map[string]string{"filename": "€uro.txt"}))
	fmt.Println("QEncoding", mime.QEncoding.Encode("utf-8", "Héllo Wörld"), mime.BEncoding.Encode("utf-8", "¡Hola!"))
	dec := new(mime.WordDecoder)
	ds, err := dec.DecodeHeader("=?utf-8?q?caf=C3=A9?= and =?utf-8?b?w6k=?=")
	fmt.Println("DecodeHeader", ds, err)

	// netip.
	for _, s := range []string{"192.168.1.10", "::1", "2001:db8::68", "::ffff:10.0.0.1", "fe80::1%eth0", "256.1.1.1", "1.2.3", "0.0.0.0", "ff02::1"} {
		a, err := netip.ParseAddr(s)
		if err != nil {
			fmt.Printf("ParseAddr(%q): %v\n", s, err)
			continue
		}
		fmt.Printf("ParseAddr(%q) = %v is4=%v is6=%v 4in6=%v loop=%v private=%v mcast=%v linklocal=%v unspec=%v zone=%q unmap=%v next=%v prev=%v bitlen=%d\n",
			s, a, a.Is4(), a.Is6(), a.Is4In6(), a.IsLoopback(), a.IsPrivate(), a.IsMulticast(), a.IsLinkLocalUnicast(), a.IsUnspecified(), a.Zone(), a.Unmap(), a.Next(), a.Prev(), a.BitLen())
	}
	for _, s := range []string{"10.0.0.0/8", "192.168.1.77/24", "2001:db8::/32", "1.2.3.4/33", "::/0"} {
		p, err := netip.ParsePrefix(s)
		if err != nil {
			fmt.Printf("ParsePrefix(%q): %v\n", s, err)
			continue
		}
		in := netip.MustParseAddr("10.1.2.3")
		fmt.Printf("ParsePrefix(%q) = %v masked=%v bits=%d contains(10.1.2.3)=%v single=%v\n", s, p, p.Masked(), p.Bits(), p.Contains(in), p.IsSingleIP())
	}
	ap, err := netip.ParseAddrPort("[::1]:8080")
	fmt.Println("AddrPort", ap, ap.Addr(), ap.Port(), err, netip.AddrPortFrom(netip.MustParseAddr("1.2.3.4"), 53))
	a1, a2 := netip.MustParseAddr("10.0.0.1"), netip.MustParseAddr("10.0.0.2")
	fmt.Println("compare", a1.Compare(a2), a2.Less(a1), a1 == netip.MustParseAddr("10.0.0.1"), netip.AddrFrom4([4]byte{127, 0, 0, 1}))

	// net/mail.
	addrs, err := mail.ParseAddressList(`"Smith, John" <john@example.com>, jane@example.org, =?utf-8?q?J=C3=B6rg?= <jorg@example.de>, Group: a@b.c, d@e.f;`)
	fmt.Println("ParseAddressList err:", err)
	for _, a := range addrs {
		fmt.Printf("  name=%q address=%q string=%q\n", a.Name, a.Address, a.String())
	}
	for _, s := range []string{"no-at-sign", "<unterminated@x", "a@b", `"quoted local"@example.com`} {
		a, err := mail.ParseAddress(s)
		fmt.Printf("ParseAddress(%q) = %v %v\n", s, a, err)
	}
}

func sortStrings(s []string) {
	for i := 1; i < len(s); i++ {
		for j := i; j > 0 && s[j] < s[j-1]; j-- {
			s[j], s[j-1] = s[j-1], s[j]
		}
	}
}
