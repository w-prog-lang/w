fn classify: int32 <- (n: int32) {
    if (n < 0) {
        return -1;
    } else if (n == 0) {
        return 0;
    } else if (n < 10) {
        return 1;
    } else {
        return 2;
    }
}

fn main: int32 <- () {
    return classify(7);
}
