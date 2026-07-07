fn sum3: int32 <- (arr: int32[3]) {
    return arr[0] + arr[1] + arr[2];
}

fn main: int32 <- () {
    var a: int32[3];
    a[0] = 1;
    a[1] = 2;
    a[2] = 3;
    return sum3(a);
}
