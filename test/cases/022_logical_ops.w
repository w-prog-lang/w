fn main: int32 <- () {
    var a := 3;
    var b := 5;
    if (a < b && b < 10) {
        if (a > 100 || b > 4) {
            return 1;
        }
    }
    return 0;
}
