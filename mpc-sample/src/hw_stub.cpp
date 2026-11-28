// The desktop simulator has no MPC hardware.
#include "hw.h"
namespace cx { std::unique_ptr<Hw> makeMpcHw() { return nullptr; } }
