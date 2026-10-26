#pragma once
// RNNoise 0.2's scalar vec.h includes this unshipped Opus header solely for
// OPUS_CLEAR. Use RNNoise's equivalent, without depending on the Opus tree.
#include "common.h"
#define OPUS_CLEAR(dst, count) RNN_CLEAR(dst, count)
