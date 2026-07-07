struct Point {
    x: int32,
    y: int32
}

fn main: int32 <- () {
    var p: Point;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;
}
