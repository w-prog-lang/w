fn main: int32 <- () {
    var sum := 0;
    loop (var i := 0; i < 10; i += 1) {
        if (i == 5) {
            continue;
        }
        sum = sum + i;
    }
    return sum;
}
