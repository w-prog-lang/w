fn main: int32 <- () {
    var arr: int32[5];
    arr[0] = 10;
    arr[1] = 20;
    var i := 2;
    arr[i] = arr[0] + arr[1];
    return arr[0] + arr[1] + arr[2];
}
