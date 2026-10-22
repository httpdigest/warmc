// Symmetric and public-key cryptography with fixed keys, so every output is
// a known-answer test: AES-128/192/256 block encryption (FIPS-197 vectors),
// the cipher modes CBC, CTR (streamed in uneven pieces) and GCM (seal, open,
// additional data, tampered tag rejection, non-standard nonce and tag sizes),
// DES and 3DES, RC4; Ed25519 keys from a fixed seed with deterministic
// signatures and verification of good and bad messages; X25519 and NIST
// P-256/P-384/P-521 ECDH shared secrets from fixed private keys; ML-KEM-768
// key derivation from a fixed seed; RSA-1024 built from fixed primes with
// PKCS#1 v1.5 signatures (deterministic) and decryption of a ciphertext
// computed here with math/big; and crypto/subtle's constant-time helpers.
//
// The elliptic-curve field arithmetic is fiat-crypto generated Go -- long
// straight-line sequences of bits.Mul64/Add64 -- and Ed25519 and X25519 use
// a 51-bit-limb field; on wasm all of that is synthesised from i64 ops.
package main

import (
	"bytes"
	"crypto"
	"crypto/aes"
	"crypto/cipher"
	"crypto/des"
	"crypto/ecdh"
	"crypto/ed25519"
	"crypto/mlkem"
	"crypto/rc4"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"fmt"
	"math/big"
)

func unhex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

func seq(n int, mul, add byte) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i)*mul + add
	}
	return b
}

func main() {
	// AES known answers (FIPS-197 appendix C).
	pt := unhex("00112233445566778899aabbccddeeff")
	for _, k := range []string{"000102030405060708090a0b0c0d0e0f", "000102030405060708090a0b0c0d0e0f1011121314151617", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"} {
		block, err := aes.NewCipher(unhex(k))
		ct := make([]byte, 16)
		block.Encrypt(ct, pt)
		back := make([]byte, 16)
		block.Decrypt(back, ct)
		fmt.Printf("AES-%d %x -> %x back=%v err=%v\n", len(k)*4, pt, ct, bytes.Equal(back, pt), err)
	}
	_, err := aes.NewCipher(make([]byte, 7))
	fmt.Println("bad key size:", err)

	key := seq(32, 7, 3)
	block, _ := aes.NewCipher(key)
	iv := seq(16, 13, 1)
	msg := []byte("The quick brown fox jumps over the lazy dog, twice over: The quick brown fox!")

	// CBC (pad to block size with PKCS#7 by hand).
	padLen := 16 - len(msg)%16
	padded := append(append([]byte{}, msg...), bytes.Repeat([]byte{byte(padLen)}, padLen)...)
	cbc := make([]byte, len(padded))
	cipher.NewCBCEncrypter(block, iv).CryptBlocks(cbc, padded)
	fmt.Printf("CBC %x\n", cbc)
	dec := make([]byte, len(cbc))
	cipher.NewCBCDecrypter(block, iv).CryptBlocks(dec, cbc)
	fmt.Printf("CBC back %q\n", dec[:len(dec)-int(dec[len(dec)-1])])

	// CTR, streamed in uneven pieces.
	ctr := make([]byte, len(msg))
	s := cipher.NewCTR(block, iv)
	for i := 0; i < len(msg); i += 7 {
		end := min(i+7, len(msg))
		s.XORKeyStream(ctr[i:end], msg[i:end])
	}
	fmt.Printf("CTR %x\n", ctr)
	back := make([]byte, len(ctr))
	cipher.NewCTR(block, iv).XORKeyStream(back, ctr)
	fmt.Printf("CTR back equal %v\n", bytes.Equal(back, msg))

	// GCM.
	gcm, _ := cipher.NewGCM(block)
	nonce := seq(12, 3, 9)
	aad := []byte("header v1")
	sealed := gcm.Seal(nil, nonce, msg, aad)
	fmt.Printf("GCM sealed %d bytes: %x\n", len(sealed), sealed)
	opened, err := gcm.Open(nil, nonce, sealed, aad)
	fmt.Printf("GCM open %q err=%v\n", opened, err)
	tampered := append([]byte{}, sealed...)
	tampered[len(tampered)-1] ^= 1
	_, err = gcm.Open(nil, nonce, tampered, aad)
	fmt.Println("GCM tampered tag:", err)
	_, err = gcm.Open(nil, nonce, sealed, []byte("other header"))
	fmt.Println("GCM wrong aad:", err)
	fmt.Printf("GCM empty plaintext tag %x\n", gcm.Seal(nil, nonce, nil, nil))
	g16, _ := cipher.NewGCMWithNonceSize(block, 16)
	fmt.Printf("GCM 16-byte nonce %x\n", g16.Seal(nil, seq(16, 1, 0), msg[:20], nil))
	g12, _ := cipher.NewGCMWithTagSize(block, 12)
	fmt.Printf("GCM 12-byte tag %x overhead=%d\n", g12.Seal(nil, nonce, msg[:20], nil), g12.Overhead())
	// FIPS 128-bit test vector (GCM spec test case 2).
	zb, _ := aes.NewCipher(make([]byte, 16))
	zg, _ := cipher.NewGCM(zb)
	fmt.Printf("GCM test case 2 %x\n", zg.Seal(nil, make([]byte, 12), make([]byte, 16), nil))

	// DES / 3DES / RC4.
	dblock, _ := des.NewCipher(unhex("133457799bbcdff1"))
	dct := make([]byte, 8)
	dblock.Encrypt(dct, unhex("0123456789abcdef"))
	fmt.Printf("DES %x\n", dct)
	tblock, _ := des.NewTripleDESCipher(seq(24, 17, 5))
	tct := make([]byte, 8)
	tblock.Encrypt(tct, []byte("8 bytes!"))
	tback := make([]byte, 8)
	tblock.Decrypt(tback, tct)
	fmt.Printf("3DES %x back %q\n", tct, tback)
	rc, _ := rc4.NewCipher([]byte("Key"))
	rcout := make([]byte, 9)
	rc.XORKeyStream(rcout, []byte("Plaintext"))
	fmt.Printf("RC4 %x\n", rcout)

	// Ed25519 (RFC 8032 test 1 seed, plus our own).
	seed := unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
	priv := ed25519.NewKeyFromSeed(seed)
	pub := priv.Public().(ed25519.PublicKey)
	sig := ed25519.Sign(priv, nil)
	fmt.Printf("ed25519 pub %x\n", pub)
	fmt.Printf("ed25519 sig(empty) %x\n", sig)
	fmt.Println("verify good:", ed25519.Verify(pub, nil, sig), "verify other msg:", ed25519.Verify(pub, []byte("x"), sig))
	for i := 0; i < 4; i++ {
		p := ed25519.NewKeyFromSeed(seq(32, byte(i+1), byte(i*31)))
		m := []byte(fmt.Sprintf("message %d", i))
		sg := ed25519.Sign(p, m)
		bad := append([]byte{}, sg...)
		bad[5] ^= 0x40
		fmt.Printf("ed25519 #%d pub=%x sig=%x ok=%v bad=%v\n", i, p.Public().(ed25519.PublicKey)[:8], sg[:16], ed25519.Verify(p.Public().(ed25519.PublicKey), m, sg), ed25519.Verify(p.Public().(ed25519.PublicKey), m, bad))
	}
	ctxSig, err := priv.Sign(nil, []byte("with context"), &ed25519.Options{Context: "ctx"})
	fmt.Printf("ed25519ctx sig %x err=%v verify=%v\n", ctxSig[:16], err, ed25519.VerifyWithOptions(pub, []byte("with context"), ctxSig, &ed25519.Options{Context: "ctx"}) == nil)

	// ECDH.
	curves := []struct {
		name string
		c    ecdh.Curve
		size int
	}{{"X25519", ecdh.X25519(), 32}, {"P256", ecdh.P256(), 32}, {"P384", ecdh.P384(), 48}, {"P521", ecdh.P521(), 66}}
	for _, cv := range curves {
		a := seq(cv.size, 3, 1)
		b := seq(cv.size, 5, 2)
		if cv.size == 66 {
			a[0], b[0] = 0, 1 // keep below the P-521 order
		}
		ka, errA := cv.c.NewPrivateKey(a)
		kb, errB := cv.c.NewPrivateKey(b)
		if errA != nil || errB != nil {
			fmt.Println(cv.name, "key error", errA, errB)
			continue
		}
		s1, _ := ka.ECDH(kb.PublicKey())
		s2, _ := kb.ECDH(ka.PublicKey())
		fmt.Printf("%s pubA=%x...\n  shared=%x equal=%v\n", cv.name, ka.PublicKey().Bytes()[:16], s1, bytes.Equal(s1, s2))
	}
	_, err = ecdh.P256().NewPrivateKey(make([]byte, 32))
	fmt.Println("P256 zero key:", err)
	_, err = ecdh.P256().NewPublicKey([]byte{4, 1, 2, 3})
	fmt.Println("P256 bad point:", err)
	// RFC 7748 X25519 test vector.
	alice, _ := ecdh.X25519().NewPrivateKey(unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"))
	bobPub, _ := ecdh.X25519().NewPublicKey(unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"))
	ss, _ := alice.ECDH(bobPub)
	fmt.Printf("RFC7748 shared %x\n", ss)

	// ML-KEM-768 from a fixed seed: key derivation is deterministic.
	dk, err := mlkem.NewDecapsulationKey768(seq(64, 11, 7))
	if err == nil {
		ek := dk.EncapsulationKey().Bytes()
		h := sha256.Sum256(ek)
		fmt.Printf("ML-KEM-768 encapsulation key %d bytes sha256=%x\n", len(ek), h)
	} else {
		fmt.Println("ML-KEM error", err)
	}

	// RSA-1024 from fixed primes.
	p, _ := new(big.Int).SetString("C355693313A153E1E30B25FC29DBBFE1391AEF0FB90689A204370AC5B08DFDF51A272565D35035008AFC6D92C428DF90018D54E76FA094F8C15AACB6AA6C2FE7", 16)
	q, _ := new(big.Int).SetString("EBA07E2180CF15F6F79755DD550F55218903BF0BCAF57CA96E95F7B3FA21BF3EEB2A7DD23E315191C5DECCBD9B6D923A6366CB68EB1E277885A803235AC874C3", 16)
	n := new(big.Int).Mul(p, q)
	e := big.NewInt(65537)
	phi := new(big.Int).Mul(new(big.Int).Sub(p, big.NewInt(1)), new(big.Int).Sub(q, big.NewInt(1)))
	d := new(big.Int).ModInverse(e, phi)
	rk := &rsa.PrivateKey{PublicKey: rsa.PublicKey{N: n, E: 65537}, D: d, Primes: []*big.Int{p, q}}
	fmt.Println("RSA validate:", rk.Validate(), "bits", n.BitLen())
	rk.Precompute()
	digest := sha256.Sum256([]byte("sign me"))
	rsig, err := rsa.SignPKCS1v15(nil, rk, crypto.SHA256, digest[:])
	fmt.Printf("RSA PKCS1v15 sig err=%v %x\n", err, rsig)
	fmt.Println("RSA verify:", rsa.VerifyPKCS1v15(&rk.PublicKey, crypto.SHA256, digest[:], rsig))
	digest[0] ^= 1
	fmt.Println("RSA verify wrong digest:", rsa.VerifyPKCS1v15(&rk.PublicKey, crypto.SHA256, digest[:], rsig))
	// Build a PKCS#1 v1.5 encryption block by hand with fixed non-zero padding.
	plain := []byte("secret message")
	k := (n.BitLen() + 7) / 8
	em := make([]byte, k)
	em[1] = 2
	for i := 2; i < k-len(plain)-1; i++ {
		em[i] = byte(i%250 + 1)
	}
	copy(em[k-len(plain):], plain)
	c := new(big.Int).Exp(new(big.Int).SetBytes(em), e, n)
	cbytes := c.FillBytes(make([]byte, k))
	fmt.Printf("RSA ciphertext %x...\n", cbytes[:24])
	got, err := rsa.DecryptPKCS1v15(nil, rk, cbytes)
	fmt.Printf("RSA decrypt %q %v\n", got, err)
	cbytes[10] ^= 0xff
	_, err = rsa.DecryptPKCS1v15(nil, rk, cbytes)
	fmt.Println("RSA decrypt corrupted:", err)

	// subtle.
	fmt.Println("ConstantTimeCompare", subtle.ConstantTimeCompare([]byte("abc"), []byte("abc")), subtle.ConstantTimeCompare([]byte("abc"), []byte("abd")), subtle.ConstantTimeCompare([]byte("a"), []byte("ab")))
	fmt.Println("ConstantTimeByteEq", subtle.ConstantTimeByteEq(7, 7), subtle.ConstantTimeEq(5, 6), "Select", subtle.ConstantTimeSelect(1, 10, 20), subtle.ConstantTimeSelect(0, 10, 20), "LessOrEq", subtle.ConstantTimeLessOrEq(3, 4))
	xb := make([]byte, 5)
	nx := subtle.XORBytes(xb, []byte("hello"), []byte("WORLD!!"))
	fmt.Printf("XORBytes %d %x\n", nx, xb)
}
