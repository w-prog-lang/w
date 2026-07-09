struct Point {
    x: int32,
    y: int32
}

struct Line {
    a: Point
}

fn make_point: Point <- (x: int32, y: int32) {
    var p: Point;
    p.x = x;
    p.y = y;
    return p;
}

fn main: int32 <- () {
    var l: Line;
    l.a = make_point(3, 4);
    return l.a.x + l.a.y;
}
