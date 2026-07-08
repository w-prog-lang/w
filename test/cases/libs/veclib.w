// a W library that itself imports a sibling library (paths are resolved
// relative to this file, so no 'libs/' prefix here)
#import <mathlib.w>

fn len_sq: int32 <- (x: int32, y: int32) {
    return square(x) + square(y);
}
