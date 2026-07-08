// a '.w' import alone must NOT unlock unchecked calls -- only '.h' does
#import <libs/mathlib.w>

fn main: int32 <- () {
    return nosuch(1);
}
