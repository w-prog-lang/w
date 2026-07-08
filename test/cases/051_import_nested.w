// veclib imports mathlib itself; importing both must merge mathlib once
// (diamond), not twice (redefinition)
#import <libs/veclib.w>
#import <libs/mathlib.w>

fn main: int32 <- () {
    return len_sq(3, 4) + square(3);
}
