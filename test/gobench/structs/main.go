// A slice of structs walked by value and by pointer: field offsets, and a
// method on a struct receiver.  Go programs are mostly this.
package main

type particle struct {
	x, y, z    float64
	vx, vy, vz float64
	id         int
}

func (p *particle) step(dt float64) {
	p.x += p.vx * dt
	p.y += p.vy * dt
	p.z += p.vz * dt
}

func main() {
	ps := make([]particle, 4096)
	for i := range ps {
		ps[i] = particle{x: float64(i), vx: 1.5, vy: -0.5, vz: 0.25, id: i}
	}
	for r := 0; r < 20000; r++ {
		for i := range ps {
			ps[i].step(0.01)
		}
	}
	var s float64
	for i := range ps {
		s += ps[i].x + ps[i].y + ps[i].z
	}
	println("s =", int(s))
}
