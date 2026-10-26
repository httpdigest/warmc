// An HTTP server in Go's own net/http, built GOOS=wasip1: what a guest that
// serves the network needs of the WASI host, driven by test/run_gohttp.py.
//
// wasip1 has no way to open a socket, so the server takes the listener
// --tcplisten hands it, the first descriptor from 3 up that fd_filestat_get
// calls a stream socket (the preopened directories come first).  Go's
// runtime is one thread: every connection, timer and the read of stdin is a
// goroutine parked in poll_oneoff until its descriptor is ready, once a read
// or write of it has said EAGAIN.  A listener that blocked would stop the
// whole program in sock_accept, and so would stdin, which runtime start-up
// makes non-blocking through fd_fdstat_set_flags, and stdout for a reader
// that is slow.
//
// Routes:
//
//	/hello          "hello, world"
//	/echo           the request's body back, once all of it has come: a
//	                client that sends all of it before it reads, as
//	                http.client does, would wait on a server that wrote as
//	                it read, as the server waited on it
//	/big?n=N        N bytes, byte i being i % 251, written 64 KB at a time
//	/file           /d/seed.txt from the preopened directory
//	/slow?ms=N      the answer after N ms, while other connections are served
//	/conns          how many connections there have been
//	/header         the request's X-Probe header
//	/print?n=N      N lines of 100 bytes to stdout, then "printed"
//
// A line "quit" on stdin shuts the server down: it stops accepting, lets the
// requests in flight finish, prints "bye" and exits 0.
package main

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"time"
)

func listener() (net.Listener, error) {
	var st syscall.Stat_t
	for fd := 3; syscall.Fstat(fd, &st) == nil; fd++ {
		if st.Filetype == syscall.FILETYPE_SOCKET_STREAM {
			return net.FileListener(os.NewFile(uintptr(fd), "listener"))
		}
	}
	return nil, fmt.Errorf("no listening socket")
}

func intArg(r *http.Request, name string) int {
	n, _ := strconv.Atoi(r.URL.Query().Get(name))
	return n
}

func main() {
	l, err := listener()
	if err != nil {
		fmt.Fprintln(os.Stderr, "gohttp:", err)
		os.Exit(1)
	}

	var conns atomic.Int64
	mux := http.NewServeMux()
	mux.HandleFunc("/hello", func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "hello, world\n")
	})
	mux.HandleFunc("/echo", func(w http.ResponseWriter, r *http.Request) {
		b, err := io.ReadAll(r.Body)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		w.Write(b)
	})
	mux.HandleFunc("/big", func(w http.ResponseWriter, r *http.Request) {
		n := intArg(r, "n")
		w.Header().Set("Content-Length", strconv.Itoa(n))
		buf := make([]byte, 64<<10)
		for i := 0; i < n; {
			k := min(len(buf), n-i)
			for j := 0; j < k; j++ {
				buf[j] = byte((i + j) % 251)
			}
			if _, err := w.Write(buf[:k]); err != nil {
				return
			}
			i += k
		}
	})
	mux.HandleFunc("/file", func(w http.ResponseWriter, r *http.Request) {
		b, err := os.ReadFile("/d/seed.txt")
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Write(b)
	})
	mux.HandleFunc("/slow", func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(time.Duration(intArg(r, "ms")) * time.Millisecond)
		io.WriteString(w, "slept\n")
	})
	mux.HandleFunc("/conns", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, conns.Load())
	})
	mux.HandleFunc("/header", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, r.Header.Get("X-Probe"))
	})
	mux.HandleFunc("/print", func(w http.ResponseWriter, r *http.Request) {
		line := strings.Repeat("x", 99) + "\n"
		for i := intArg(r, "n"); i > 0; i-- {
			io.WriteString(os.Stdout, line)
		}
		io.WriteString(w, "printed\n")
	})

	srv := &http.Server{
		Handler: mux,
		ConnState: func(c net.Conn, s http.ConnState) {
			if s == http.StateNew {
				conns.Add(1)
			}
		},
	}
	done := make(chan struct{})
	go func() {
		in := bufio.NewScanner(os.Stdin)
		for in.Scan() {
			if in.Text() == "quit" {
				break
			}
		}
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		srv.Shutdown(ctx)
		close(done)
	}()

	fmt.Fprintln(os.Stderr, "gohttp: serving")
	if err := srv.Serve(l); err != http.ErrServerClosed {
		fmt.Fprintln(os.Stderr, "gohttp:", err)
		os.Exit(1)
	}
	<-done
	fmt.Println("bye")
}
