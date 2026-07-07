struct Point {
    x: int32,
    y: int32
}

fn main: int32 <- () {
    var arr: Point[2];
    arr[0].x = 1;
    arr[0].y = 2;
    arr[1].x = 10;
    arr[1].y = 20;
    return arr[0].x + arr[0].y + arr[1].x + arr[1].y;
}
