const MAX: i32 = 100;
static COUNT: i32 = 0;

struct Point {
    x: i32,
    y: i32,
}

impl Point {
    fn new(x: i32, y: i32) -> Point {
        Point { x, y }
    }
    fn norm2(&self) -> i32 {
        self.x * self.x + self.y * self.y
    }
    fn shift(&mut self, dx: i32) {
        self.x += dx;
    }
}

enum Shape {
    Circle(f64),
    Rect { w: f64, h: f64 },
    Empty,
}

fn add(a: i32, b: i32) -> i32 {
    a + b
}

fn early(x: i32) -> i32 {
    if x < 0 { return 0; }
    x
}

fn main() {
    let x = 5;
    let mut y = 5;
    let z: i32 = 10;
    let t: (i32, bool) = (1, true);
    let arr: [i32; 5] = [1, 2, 3, 4, 5];

    if x > 0 { y += 1; } else if x < 0 { y -= 1; } else { }
    while y < 10 { y += 1; }
    loop { if y > 20 { break; } y += 1; }
    for i in 0..10 { y += i; }
    for i in 0..=10 { y += i; }
    let b = if x > 0 { 1 } else { -1 };

    let mut p = Point::new(1, 2);
    p.shift(3);
    let n = p.norm2();

    let shape = Shape::Rect { w: 1.0, h: 2.0 };
    let area = match shape {
        Shape::Circle(r) => r * r * 3.14,
        Shape::Rect { w, h } => w * h,
        Shape::Empty => 0.0,
    };

    let r = &x;
    let m = &mut y;
    *m = 10;

    println!("x = {}", x);
    print!("no newline");
}
