fn add: int128 <- (a: int64, b: int64) {
    return a + b;
}

fn main: int32 <- () {
    a;
    a1 = 42;
    var a2 := 42;
    a3 := 255;
    a2 := 41;
    a2 = 41;
    b: int64 = 12;
    var c: int64;
    c = b;
    var sum := add(c, a2);
    if (sum >= 30) {
        return sum;
    }
}
