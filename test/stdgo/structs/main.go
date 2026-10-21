// A slice of structs walked by pointer, with a method on the struct; the same
// source as test/gobench/structs.  Field offsets and a call per element, which
// is most Go programs.
package main

import "fmt"

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
	fmt.Println("s =", int(s))
}
