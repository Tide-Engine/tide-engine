#pragma once

// The bytes a [Linear] image stores, without a pow a channel. A marked image's RGB is
//
//     byte(tide_todisplay_f(tide_tolinear_f(straight) * alpha / 255))
//
// with byte(v) = (uint8_t)(v * 255.0f + 0.5f): what tidec packs for an asset, and what the draw list makes of a
// Color grid's cells. Both functions never go down as their argument goes up, so each byte's values are one run
// of floats, and the two tables below say where each run starts: the byte is found by comparing, and is the
// byte the formula gives for every float (not nearly: the same one). They're numbers of tide_pow_f, so a change
// to it or to the transfer functions remakes them: tests/test_math.c (math_transfer_bytes) checks every entry
// against the formula, on every platform, and says how they were made and proved.

#include <stdbool.h>
#include <stdint.h>
#include "tide/math.h"

// tide_transfer_display_from[b]: the least linear value whose display byte, byte(tide_todisplay_f(v)), is b.
static const float tide_transfer_display_from[256] = {
    0.0f, 0x1.3e4566p-13f, 0x1.dd681cp-12f, 0x1.8dd6c2p-11f, 0x1.167cbap-10f, 0x1.660e16p-10f,
    0x1.b59f6ep-10f, 0x1.029864p-9f, 0x1.2a6112p-9f, 0x1.5229bep-9f, 0x1.79f26cp-9f, 0x1.a1e5a2p-9f,
    0x1.cbf734p-9f, 0x1.f86808p-9f, 0x1.13a0cp-8f, 0x1.2c4666p-8f, 0x1.46297cp-8f, 0x1.614e62p-8f,
    0x1.7db96cp-8f, 0x1.9b6ed8p-8f, 0x1.ba72ccp-8f, 0x1.dac95cp-8f, 0x1.fc7688p-8f, 0x1.0fbf2p-7f,
    0x1.21f232p-7f, 0x1.34d664p-7f, 0x1.486d8ep-7f, 0x1.5cb98ep-7f, 0x1.71bc32p-7f, 0x1.877746p-7f,
    0x1.9dec8cp-7f, 0x1.b51dc4p-7f, 0x1.cd0ca8p-7f, 0x1.e5bae6p-7f, 0x1.ff2a24p-7f, 0x1.0cae06p-6f,
    0x1.1a292p-6f, 0x1.28072cp-6f, 0x1.3648f8p-6f, 0x1.44ef5p-6f, 0x1.53faf2p-6f, 0x1.636ca8p-6f,
    0x1.734532p-6f, 0x1.838552p-6f, 0x1.942dc6p-6f, 0x1.a53f48p-6f, 0x1.b6ba9ap-6f, 0x1.c8a068p-6f,
    0x1.daf16cp-6f, 0x1.edae5ep-6f, 0x1.006bf8p-5f, 0x1.0a376cp-5f, 0x1.1439dcp-5f, 0x1.1e73a2p-5f,
    0x1.28e514p-5f, 0x1.338e8ap-5f, 0x1.3e7056p-5f, 0x1.498ad4p-5f, 0x1.54de48p-5f, 0x1.606b0cp-5f,
    0x1.6c3172p-5f, 0x1.7831ccp-5f, 0x1.846c68p-5f, 0x1.90e196p-5f, 0x1.9d91a6p-5f, 0x1.aa7ce8p-5f,
    0x1.b7a3a6p-5f, 0x1.c50632p-5f, 0x1.d2a4d4p-5f, 0x1.e07fdcp-5f, 0x1.ee979cp-5f, 0x1.fcec4ep-5f,
    0x1.05bf24p-4f, 0x1.0d26e8p-4f, 0x1.14ad98p-4f, 0x1.1c535ap-4f, 0x1.24185p-4f, 0x1.2bfcap-4f,
    0x1.34006cp-4f, 0x1.3c23d8p-4f, 0x1.446708p-4f, 0x1.4cca2p-4f, 0x1.554d4p-4f, 0x1.5df08ep-4f,
    0x1.66b42ep-4f, 0x1.6f983ap-4f, 0x1.789cd8p-4f, 0x1.81c22cp-4f, 0x1.8b0854p-4f, 0x1.946f74p-4f,
    0x1.9df7aep-4f, 0x1.a7a12p-4f, 0x1.b16becp-4f, 0x1.bb5832p-4f, 0x1.c56614p-4f, 0x1.cf95b2p-4f,
    0x1.d9e72ap-4f, 0x1.e45a9ep-4f, 0x1.eef034p-4f, 0x1.f9a7fcp-4f, 0x1.02411p-3f, 0x1.07bf5ep-3f,
    0x1.0d4ef8p-3f, 0x1.12efeep-3f, 0x1.18a24ep-3f, 0x1.1e6628p-3f, 0x1.243b8cp-3f, 0x1.2a2288p-3f,
    0x1.301b2ap-3f, 0x1.362584p-3f, 0x1.3c41a2p-3f, 0x1.426f96p-3f, 0x1.48af6ep-3f, 0x1.4f0134p-3f,
    0x1.5564f8p-3f, 0x1.5bdacep-3f, 0x1.6262cp-3f, 0x1.68fcdep-3f, 0x1.6fa93ap-3f, 0x1.7667dap-3f,
    0x1.7d38dp-3f, 0x1.841c28p-3f, 0x1.8b11f4p-3f, 0x1.921a42p-3f, 0x1.99351ep-3f, 0x1.a06296p-3f,
    0x1.a7a2b8p-3f, 0x1.aef592p-3f, 0x1.b65b32p-3f, 0x1.bdd3a4p-3f, 0x1.c55ef8p-3f, 0x1.ccfd3ap-3f,
    0x1.d4ae8p-3f, 0x1.dc72c6p-3f, 0x1.e44a24p-3f, 0x1.ec34a6p-3f, 0x1.f4325ap-3f, 0x1.fc434cp-3f,
    0x1.0233c4p-2f, 0x1.064f9p-2f, 0x1.0a750ep-2f, 0x1.0ea444p-2f, 0x1.12dd3cp-2f, 0x1.171ff8p-2f,
    0x1.1b6c82p-2f, 0x1.1fc2ep-2f, 0x1.242316p-2f, 0x1.288d2cp-2f, 0x1.2d0128p-2f, 0x1.317f12p-2f,
    0x1.3606eep-2f, 0x1.3a98c2p-2f, 0x1.3f3496p-2f, 0x1.43da7p-2f, 0x1.488a54p-2f, 0x1.4d444ap-2f,
    0x1.52085ap-2f, 0x1.56d686p-2f, 0x1.5baed6p-2f, 0x1.609152p-2f, 0x1.657e02p-2f, 0x1.6a74e4p-2f,
    0x1.6f7602p-2f, 0x1.748164p-2f, 0x1.79970cp-2f, 0x1.7eb704p-2f, 0x1.83e14ep-2f, 0x1.8915f4p-2f,
    0x1.8e54fap-2f, 0x1.939e64p-2f, 0x1.98f23cp-2f, 0x1.9e5084p-2f, 0x1.a3b944p-2f, 0x1.a92c82p-2f,
    0x1.aeaa42p-2f, 0x1.b4328ap-2f, 0x1.b9c562p-2f, 0x1.bf62cep-2f, 0x1.c50ad4p-2f, 0x1.cabd78p-2f,
    0x1.d07ac4p-2f, 0x1.d642b8p-2f, 0x1.dc155ep-2f, 0x1.e1f2bap-2f, 0x1.e7dad2p-2f, 0x1.edcdacp-2f,
    0x1.f3cb4cp-2f, 0x1.f9d3cp-2f, 0x1.ffe6fep-2f, 0x1.03028ap-1f, 0x1.061704p-1f, 0x1.0930fp-1f,
    0x1.0c504ep-1f, 0x1.0f7524p-1f, 0x1.129f74p-1f, 0x1.15cf3ep-1f, 0x1.190488p-1f, 0x1.1c3f54p-1f,
    0x1.1f7fa4p-1f, 0x1.22c57cp-1f, 0x1.2610dcp-1f, 0x1.2961c8p-1f, 0x1.2cb844p-1f, 0x1.301452p-1f,
    0x1.3375f2p-1f, 0x1.36dd2ap-1f, 0x1.3a49fcp-1f, 0x1.3dbc6ap-1f, 0x1.413476p-1f, 0x1.44b224p-1f,
    0x1.483576p-1f, 0x1.4bbe6ep-1f, 0x1.4f4d1p-1f, 0x1.52e15cp-1f, 0x1.567b58p-1f, 0x1.5a1b08p-1f,
    0x1.5dc066p-1f, 0x1.616b7cp-1f, 0x1.651c4ap-1f, 0x1.68d2d2p-1f, 0x1.6c8f18p-1f, 0x1.70511ep-1f,
    0x1.7418e6p-1f, 0x1.77e674p-1f, 0x1.7bb9cap-1f, 0x1.7f92eap-1f, 0x1.8371d6p-1f, 0x1.87569p-1f,
    0x1.8b411ep-1f, 0x1.8f317ep-1f, 0x1.9327b4p-1f, 0x1.9723c4p-1f, 0x1.9b25bp-1f, 0x1.9f2d7ap-1f,
    0x1.a33b22p-1f, 0x1.a74ebp-1f, 0x1.ab682p-1f, 0x1.af877ap-1f, 0x1.b3acbcp-1f, 0x1.b7d7ecp-1f,
    0x1.bc090ap-1f, 0x1.c0401ap-1f, 0x1.c47d22p-1f, 0x1.c8c016p-1f, 0x1.cd090cp-1f, 0x1.d157f4p-1f,
    0x1.d5ace2p-1f, 0x1.da07c6p-1f, 0x1.de68b6p-1f, 0x1.e2cfap-1f, 0x1.e73c9ap-1f, 0x1.ebaf94p-1f,
    0x1.f028a4p-1f, 0x1.f4a7b6p-1f, 0x1.f92ce2p-1f, 0x1.fdb81cp-1f,
};

// tide_transfer_same_from[b]: the least straight value an opaque pixel stores as b,
// byte(tide_todisplay_f(tide_tolinear_f(v))).
// Not b / 255 - 0.5 / 255: the two pows leave a value a hair off, which moves a few floats at each edge.
static const float tide_transfer_same_from[256] = {
    0.0f, 0x1.0101p-9f, 0x1.818182p-8f, 0x1.414142p-7f, 0x1.c1c1cp-7f, 0x1.212122p-6f,
    0x1.616162p-6f, 0x1.a1a1a2p-6f, 0x1.e1e1e4p-6f, 0x1.111112p-5f, 0x1.313132p-5f, 0x1.515158p-5f,
    0x1.717178p-5f, 0x1.919192p-5f, 0x1.b1b1b8p-5f, 0x1.d1d1d8p-5f, 0x1.f1f1fap-5f, 0x1.09090cp-4f,
    0x1.19191ap-4f, 0x1.292928p-4f, 0x1.393938p-4f, 0x1.494948p-4f, 0x1.595958p-4f, 0x1.696968p-4f,
    0x1.797978p-4f, 0x1.898988p-4f, 0x1.999998p-4f, 0x1.a9a9a8p-4f, 0x1.b9b9b8p-4f, 0x1.c9c9c8p-4f,
    0x1.d9d9d8p-4f, 0x1.e9e9e8p-4f, 0x1.f9f9f8p-4f, 0x1.050508p-3f, 0x1.0d0d1p-3f, 0x1.151518p-3f,
    0x1.1d1d2p-3f, 0x1.252528p-3f, 0x1.2d2d3p-3f, 0x1.353538p-3f, 0x1.3d3d4p-3f, 0x1.454548p-3f,
    0x1.4d4d5p-3f, 0x1.555558p-3f, 0x1.5d5d6p-3f, 0x1.656568p-3f, 0x1.6d6d7p-3f, 0x1.757578p-3f,
    0x1.7d7d8p-3f, 0x1.858588p-3f, 0x1.8d8d9p-3f, 0x1.95959cp-3f, 0x1.9d9da4p-3f, 0x1.a5a5a8p-3f,
    0x1.adadbp-3f, 0x1.b5b5b8p-3f, 0x1.bdbdbcp-3f, 0x1.c5c5c8p-3f, 0x1.cdcddp-3f, 0x1.d5d5d8p-3f,
    0x1.ddddep-3f, 0x1.e5e5e8p-3f, 0x1.ededfp-3f, 0x1.f5f5f8p-3f, 0x1.fdfep-3f, 0x1.030304p-2f,
    0x1.070708p-2f, 0x1.0b0b0cp-2f, 0x1.0f0f1p-2f, 0x1.131314p-2f, 0x1.17171ap-2f, 0x1.1b1b1cp-2f,
    0x1.1f1f2p-2f, 0x1.232324p-2f, 0x1.272728p-2f, 0x1.2b2b2cp-2f, 0x1.2f2f3p-2f, 0x1.333334p-2f,
    0x1.373738p-2f, 0x1.3b3b3cp-2f, 0x1.3f3f4p-2f, 0x1.434344p-2f, 0x1.474748p-2f, 0x1.4b4b4cp-2f,
    0x1.4f4f5p-2f, 0x1.535354p-2f, 0x1.575758p-2f, 0x1.5b5b5cp-2f, 0x1.5f5f6p-2f, 0x1.636364p-2f,
    0x1.676768p-2f, 0x1.6b6b6cp-2f, 0x1.6f6f7p-2f, 0x1.737374p-2f, 0x1.777778p-2f, 0x1.7b7b7cp-2f,
    0x1.7f7f8p-2f, 0x1.838382p-2f, 0x1.878788p-2f, 0x1.8b8b8cp-2f, 0x1.8f8f9p-2f, 0x1.939394p-2f,
    0x1.979798p-2f, 0x1.9b9b9cp-2f, 0x1.9f9fap-2f, 0x1.a3a3a4p-2f, 0x1.a7a7a8p-2f, 0x1.ababacp-2f,
    0x1.afafbp-2f, 0x1.b3b3b4p-2f, 0x1.b7b7b8p-2f, 0x1.bbbbbap-2f, 0x1.bfbfcp-2f, 0x1.c3c3c4p-2f,
    0x1.c7c7c8p-2f, 0x1.cbcbccp-2f, 0x1.cfcfdp-2f, 0x1.d3d3d4p-2f, 0x1.d7d7d8p-2f, 0x1.dbdbdcp-2f,
    0x1.dfdfep-2f, 0x1.e3e3e4p-2f, 0x1.e7e7e8p-2f, 0x1.ebebecp-2f, 0x1.efeffp-2f, 0x1.f3f3f4p-2f,
    0x1.f7f7f8p-2f, 0x1.fbfbfcp-2f, 0x1.p-1f, 0x1.020202p-1f, 0x1.040404p-1f, 0x1.060604p-1f,
    0x1.08080ap-1f, 0x1.0a0a0ap-1f, 0x1.0c0c0cp-1f, 0x1.0e0e0ep-1f, 0x1.10101p-1f, 0x1.121212p-1f,
    0x1.141414p-1f, 0x1.161616p-1f, 0x1.181818p-1f, 0x1.1a1a1ap-1f, 0x1.1c1c1cp-1f, 0x1.1e1e1ep-1f,
    0x1.20202p-1f, 0x1.222222p-1f, 0x1.242424p-1f, 0x1.262626p-1f, 0x1.282828p-1f, 0x1.2a2a2ap-1f,
    0x1.2c2c2cp-1f, 0x1.2e2e2ep-1f, 0x1.30303p-1f, 0x1.323232p-1f, 0x1.343434p-1f, 0x1.363636p-1f,
    0x1.383838p-1f, 0x1.3a3a3ap-1f, 0x1.3c3c3cp-1f, 0x1.3e3e3cp-1f, 0x1.40404p-1f, 0x1.424242p-1f,
    0x1.444444p-1f, 0x1.464646p-1f, 0x1.484848p-1f, 0x1.4a4a4ap-1f, 0x1.4c4c4cp-1f, 0x1.4e4e4ep-1f,
    0x1.50505p-1f, 0x1.525252p-1f, 0x1.545454p-1f, 0x1.565656p-1f, 0x1.585858p-1f, 0x1.5a5a5ap-1f,
    0x1.5c5c5cp-1f, 0x1.5e5e5ep-1f, 0x1.60606p-1f, 0x1.626262p-1f, 0x1.646464p-1f, 0x1.666666p-1f,
    0x1.686868p-1f, 0x1.6a6a6ap-1f, 0x1.6c6c6cp-1f, 0x1.6e6e6ep-1f, 0x1.70707p-1f, 0x1.727272p-1f,
    0x1.747472p-1f, 0x1.767678p-1f, 0x1.787878p-1f, 0x1.7a7a7ap-1f, 0x1.7c7c7cp-1f, 0x1.7e7e7ep-1f,
    0x1.80808p-1f, 0x1.828282p-1f, 0x1.848484p-1f, 0x1.868686p-1f, 0x1.888888p-1f, 0x1.8a8a8ap-1f,
    0x1.8c8c8cp-1f, 0x1.8e8e8ep-1f, 0x1.90909p-1f, 0x1.929292p-1f, 0x1.949494p-1f, 0x1.969696p-1f,
    0x1.989898p-1f, 0x1.9a9a9ap-1f, 0x1.9c9c9cp-1f, 0x1.9e9e9ep-1f, 0x1.a0a0ap-1f, 0x1.a2a2a2p-1f,
    0x1.a4a4a4p-1f, 0x1.a6a6a6p-1f, 0x1.a8a8a8p-1f, 0x1.aaaaaap-1f, 0x1.acacaap-1f, 0x1.aeaeaep-1f,
    0x1.b0b0bp-1f, 0x1.b2b2b2p-1f, 0x1.b4b4b4p-1f, 0x1.b6b6b6p-1f, 0x1.b8b8b8p-1f, 0x1.bababap-1f,
    0x1.bcbcbcp-1f, 0x1.bebebep-1f, 0x1.c0c0cp-1f, 0x1.c2c2c2p-1f, 0x1.c4c4c4p-1f, 0x1.c6c6c6p-1f,
    0x1.c8c8c8p-1f, 0x1.cacacap-1f, 0x1.ccccccp-1f, 0x1.cececep-1f, 0x1.d0d0dp-1f, 0x1.d2d2d2p-1f,
    0x1.d4d4d4p-1f, 0x1.d6d6d6p-1f, 0x1.d8d8d8p-1f, 0x1.dadadap-1f, 0x1.dcdcdcp-1f, 0x1.dededep-1f,
    0x1.e0e0ep-1f, 0x1.e2e2ep-1f, 0x1.e4e4e6p-1f, 0x1.e6e6e6p-1f, 0x1.e8e8eap-1f, 0x1.eaeaeap-1f,
    0x1.ececeep-1f, 0x1.eeeeeep-1f, 0x1.f0f0f2p-1f, 0x1.f2f2f2p-1f, 0x1.f4f4f6p-1f, 0x1.f6f6f6p-1f,
    0x1.f8f8fap-1f, 0x1.fafafap-1f, 0x1.fcfcfep-1f, 0x1.fefefep-1f,
};

// tide_transfer_display_guess[i]: the display byte of the float whose bits are 0x39000000 + (i << 18), from 2^-13
// to 1 by thirty-seconds of an octave: where tide_transfer_display_byte starts looking.
static const uint8_t tide_transfer_display_guess[417] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9,
    10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15,
    15, 16, 16, 16, 16, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21,
    22, 22, 23, 23, 23, 24, 24, 25, 25, 25, 26, 26, 27, 27, 27, 28, 28, 29, 29, 29, 30, 30, 30, 31,
    31, 31, 32, 32, 32, 33, 33, 33, 34, 34, 35, 35, 36, 36, 37, 38, 38, 39, 39, 40, 40, 41, 41, 42,
    42, 43, 43, 44, 44, 45, 45, 46, 46, 46, 47, 47, 48, 48, 49, 49, 49, 50, 51, 52, 53, 53, 54, 55,
    56, 56, 57, 58, 58, 59, 60, 60, 61, 62, 62, 63, 64, 64, 65, 66, 66, 67, 67, 68, 68, 69, 70, 70,
    71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 85, 86, 87, 88, 89, 90, 91, 91, 92,
    93, 94, 95, 95, 96, 97, 98, 98, 99, 101, 102, 103, 105, 106, 107, 109, 110, 111, 113, 114, 115, 116, 118, 119,
    120, 121, 122, 123, 124, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 139, 141, 143, 145, 146, 148, 150,
    152, 153, 155, 157, 158, 160, 162, 163, 165, 166, 168, 169, 171, 172, 174, 175, 177, 178, 179, 181, 182, 184, 185, 186,
    188, 190, 193, 195, 198, 200, 202, 205, 207, 209, 212, 214, 216, 218, 220, 223, 225, 227, 229, 231, 233, 235, 237, 239,
    240, 242, 244, 246, 248, 250, 251, 253, 255,
};

// byte(tide_todisplay_f(light)): a linear value as the byte an sRGB image stores. The guess is the byte of the
// first float with `light`'s exponent and top five bits of mantissa, so the byte is it or a few past it.
static inline uint8_t tide_transfer_display_byte(const float light)
{
    const float x = tide_saturate_f(light);
    const uint32_t bits = tide_f_bits(x);
    if (bits < 0x39000000u) return 0u; // Under 2^-13, which is under the least value of byte 1
    uint32_t byte = tide_transfer_display_guess[(bits - 0x39000000u) >> 18];
    while (byte < 255u && tide_transfer_display_from[byte + 1u] <= x) byte++;
    return (uint8_t)byte;
}

// A straight display-encoded channel under coverage of `alpha` in 255, as the byte a marked image stores:
// decoded, times the coverage, encoded. No pow for a clear pixel or an opaque one, and one for the rest.
static inline uint8_t tide_transfer_premultiplied_byte(const float straight, const uint8_t alpha)
{
    if (alpha == 0u) return 0u;
    if (alpha == 255u) { // There and back: the nearest byte, but for the few floats at a byte's edge
        const float x = tide_saturate_f(straight);
        uint32_t byte = (uint32_t)(x * 255.0f + 0.5f);
        while (byte > 0u && tide_transfer_same_from[byte] > x) byte--;
        while (byte < 255u && tide_transfer_same_from[byte + 1u] <= x) byte++;
        return (uint8_t)byte;
    }
    return tide_transfer_display_byte(tide_tolinear_f(straight) * ((float)alpha / 255.0f));
}

// Each byte decoded, tide_tolinear_f(b / 255.0f): what a mip's average adds up.
typedef struct tide_transfer_table {
    float linear[256];
} tide_transfer_table;

static inline void tide_transfer_table_make(tide_transfer_table *table)
{
    for (int b = 0; b < 256; b++) table->linear[b] = tide_tolinear_f((float)b / 255.0f);
}
