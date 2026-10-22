// Classes: fields with initialisers, constructors with parameter properties,
// three levels of inheritance, super() and super.method() calls, overriding
// at every level, abstract classes and methods, virtual dispatch through a
// base-typed reference (AssemblyScript reads the runtime class id from the
// object header and switches on it, then binaryen inlines the switch), `instanceof`
// on a base reference (a class-id range check), downcasts with `<T>` after
// the check, nullable fields, and objects holding other objects.  Every call
// prints, so a dispatch to the wrong override shows up by name.
import { print } from "../wasi";

abstract class Animal {
  static count: i32 = 0;
  legs: i32 = 4;
  id: i32;
  constructor(public name: string) {
    Animal.count++;
    this.id = Animal.count;
  }
  abstract sound(): string;
  describe(): string {
    return "#" + this.id.toString() + " " + this.name + " says " + this.sound() +
      " on " + this.legs.toString() + " legs";
  }
  weight(): f64 { return 1.0; }
}

class Dog extends Animal {
  constructor(name: string, public tricks: i32) { super(name); }
  sound(): string { return "woof"; }
  weight(): f64 { return 20.0 + <f64>this.tricks; }
}

class Puppy extends Dog {
  constructor(name: string) { super(name, 1); this.legs = 4; }
  sound(): string { return "yip(" + super.sound() + ")"; }
  describe(): string { return "puppy " + super.describe(); }
}

class Bird extends Animal {
  canFly: bool;
  constructor(name: string, canFly: bool) { super(name); this.legs = 2; this.canFly = canFly; }
  sound(): string { return this.canFly ? "tweet" : "squawk"; }
  weight(): f64 { return 0.5; }
}

class Penguin extends Bird {
  constructor() { super("penguin", false); }
  describe(): string { return super.describe() + " and swims"; }
  weight(): f64 { return super.weight() * 60.0; }
}

class Snake extends Animal {
  constructor(public length: f64) { super("snake"); this.legs = 0; }
  sound(): string { return "hiss"; }
}

// Linked objects and nullable references.
class Node {
  next: Node | null = null;
  constructor(public value: i32) {}
}

class Stack {
  top: Node | null = null;
  size: i32 = 0;
  push(v: i32): void {
    let n = new Node(v);
    n.next = this.top;
    this.top = n;
    this.size++;
  }
  pop(): i32 {
    let t = this.top;
    if (t === null) return -1;
    this.top = t.next;
    this.size--;
    return t.value;
  }
  sum(): i32 {
    let s = 0;
    let n = this.top;
    while (n !== null) { s += n.value; n = n.next; }
    return s;
  }
}

// A class with a method that returns `this`, for chaining.
class Builder {
  parts: string[] = [];
  add(s: string): Builder { this.parts.push(s); return this; }
  addNum(n: i32): Builder { this.parts.push(n.toString()); return this; }
  build(): string { return this.parts.join("-"); }
}

function classify(a: Animal): string {
  let s = "";
  if (a instanceof Dog) s += "Dog ";
  if (a instanceof Puppy) s += "Puppy ";
  if (a instanceof Bird) s += "Bird ";
  if (a instanceof Penguin) s += "Penguin ";
  if (a instanceof Snake) s += "Snake ";
  if (a instanceof Animal) s += "Animal";
  if (a instanceof Dog) s += " tricks=" + (<Dog>a).tricks.toString();
  if (a instanceof Snake) s += " length=" + (<Snake>a).length.toString();
  return s;
}

function run(): void {
  let zoo: Animal[] = [
    new Dog("rex", 3), new Bird("robin", true), new Puppy("bit"), new Penguin(),
    new Snake(2.5), new Dog("fido", 0), new Bird("emu", false), new Puppy("pip"),
  ];
  for (let i = 0; i < zoo.length; i++) {
    print(zoo[i].describe());
    print("  " + classify(zoo[i]) + " weight=" + zoo[i].weight().toString());
  }
  print("count=" + Animal.count.toString());
  // Dispatch in a loop over a pseudo-random order.
  let x: u32 = 7;
  let tally = new Map<string, i32>();
  let wsum = 0.0;
  for (let k = 0; k < 200; k++) {
    x = x * 1103515245 + 12345;
    let a = zoo[(x >> 16) % <u32>zoo.length];
    let snd = a.sound();
    tally.set(snd, tally.has(snd) ? tally.get(snd) + 1 : 1);
    wsum += a.weight();
  }
  let keys = tally.keys();
  for (let i = 0; i < keys.length; i++) print("tally " + keys[i] + " " + tally.get(keys[i]).toString());
  print("wsum " + wsum.toString());

  let st = new Stack();
  for (let i = 1; i <= 20; i++) {
    st.push(i * i);
    if (i % 3 == 0) print("pop " + st.pop().toString() + " size=" + st.size.toString() + " sum=" + st.sum().toString());
  }
  while (st.size > 0) {
    let v = st.pop();
    if (st.size % 4 == 0) print("drain " + v.toString() + " size=" + st.size.toString());
  }
  print("empty pop " + st.pop().toString());
  print(new Builder().add("a").addNum(1).add("b").addNum(-2).add("c").build());

  // Field mutation through a base reference.
  for (let i = 0; i < zoo.length; i++) {
    zoo[i].legs += i;
    zoo[i].name = zoo[i].name.toUpperCase();
  }
  for (let i = 0; i < zoo.length; i++) print(zoo[i].describe());
}
run();
