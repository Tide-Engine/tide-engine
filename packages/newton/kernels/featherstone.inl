// Generated from NVIDIA Newton; see ../UPSTREAM.md.
// SPDX-FileCopyrightText: Copyright (c) 2025 The Newton Developers
// SPDX-License-Identifier: Apache-2.0
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "../math_bindings.h"
#define WP_API
#define WP_NO_BFLOAT16

#define WP_TILE_BLOCK_DIM 1
#define WP_NO_CRT
#include "../vendor/warp/builtin.h"
#include "../vendor/warp/deterministic.h"

#define builtin_tid1d() wp::linear_tid<wp_linear_tid>(task_index, dim)
#define builtin_tid2d(x, y) wp::tid(x, y, task_index, dim)
#define builtin_tid3d(x, y, z) wp::tid(x, y, z, task_index, dim)
#define builtin_tid4d(x, y, z, w) wp::tid(x, y, z, w, task_index, dim)

#define builtin_block_dim() wp::block_dim()


// avoid namespacing of float type for casting to float type, this is to avoid wp::float(x), which is not valid in C++
#define float(x) cast_float(x)
#define adj_float(x, adj_x, adj_ret) adj_cast_float(x, adj_x, adj_ret)

#define int(x) cast_int(x)
#define adj_int(x, adj_x, adj_ret) adj_cast_int(x, adj_x, adj_ret)


// newton/_src/solvers/featherstone/kernels.py:584
static void jcalc_integrate_0(
    wp::int32 var_parent,
    wp::transform_t<wp::float32> var_joint_X_c,
    wp::vec_t<3, wp::float32> var_body_com_child,
    wp::int32 var_type,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::float32> var_joint_qdd,
    wp::int32 var_coord_start,
    wp::int32 var_dof_start,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::float32 var_dt,
    wp::array_t<wp::float32> var_joint_q_new,
    wp::array_t<wp::float32> var_joint_qd_new)
{
    //---------
    // primal vars
    const wp::int32 var_0 = 3;
    bool var_1{};
    bool var_2{};
    const wp::int32 var_3 = 0;
    bool var_4{};
    const wp::int32 var_5 = 1;
    bool var_6{};
    wp::float32* var_7{};
    wp::float32 var_8{};
    wp::float32 var_9{};
    wp::float32* var_10{};
    wp::float32 var_11{};
    wp::float32 var_12{};
    wp::float32* var_13{};
    wp::float32 var_14{};
    wp::float32 var_15{};
    wp::float32 var_16{};
    wp::float32 var_17{};
    wp::float32 var_18{};
    wp::float32 var_19{};
    const wp::int32 var_20 = 2;
    bool var_21{};
    const wp::int32 var_22 = 0;
    wp::int32 var_23{};
    wp::float32* var_24{};
    const wp::int32 var_25 = 1;
    wp::int32 var_26{};
    wp::float32* var_27{};
    const wp::int32 var_28 = 2;
    wp::int32 var_29{};
    wp::float32* var_30{};
    wp::vec_t<3, wp::float32> var_31{};
    wp::float32 var_32{};
    wp::float32 var_33{};
    wp::float32 var_34{};
    const wp::int32 var_35 = 0;
    wp::int32 var_36{};
    wp::float32* var_37{};
    const wp::int32 var_38 = 1;
    wp::int32 var_39{};
    wp::float32* var_40{};
    const wp::int32 var_41 = 2;
    wp::int32 var_42{};
    wp::float32* var_43{};
    wp::vec_t<3, wp::float32> var_44{};
    wp::float32 var_45{};
    wp::float32 var_46{};
    wp::float32 var_47{};
    const wp::int32 var_48 = 0;
    wp::int32 var_49{};
    wp::float32* var_50{};
    const wp::int32 var_51 = 1;
    wp::int32 var_52{};
    wp::float32* var_53{};
    const wp::int32 var_54 = 2;
    wp::int32 var_55{};
    wp::float32* var_56{};
    const wp::int32 var_57 = 3;
    wp::int32 var_58{};
    wp::float32* var_59{};
    wp::quat_t<wp::float32> var_60{};
    wp::float32 var_61{};
    wp::float32 var_62{};
    wp::float32 var_63{};
    wp::float32 var_64{};
    wp::vec_t<3, wp::float32> var_65{};
    wp::vec_t<3, wp::float32> var_66{};
    const wp::float32 var_67 = 0.0;
    wp::quat_t<wp::float32> var_68{};
    wp::quat_t<wp::float32> var_69{};
    const wp::float32 var_70 = 0.5;
    wp::quat_t<wp::float32> var_71{};
    wp::quat_t<wp::float32> var_72{};
    wp::quat_t<wp::float32> var_73{};
    wp::quat_t<wp::float32> var_74{};
    const wp::int32 var_75 = 0;
    wp::float32 var_76{};
    const wp::int32 var_77 = 0;
    wp::int32 var_78{};
    const wp::int32 var_79 = 1;
    wp::float32 var_80{};
    const wp::int32 var_81 = 1;
    wp::int32 var_82{};
    const wp::int32 var_83 = 2;
    wp::float32 var_84{};
    const wp::int32 var_85 = 2;
    wp::int32 var_86{};
    const wp::int32 var_87 = 3;
    wp::float32 var_88{};
    const wp::int32 var_89 = 3;
    wp::int32 var_90{};
    const wp::int32 var_91 = 0;
    wp::float32 var_92{};
    const wp::int32 var_93 = 0;
    wp::int32 var_94{};
    const wp::int32 var_95 = 1;
    wp::float32 var_96{};
    const wp::int32 var_97 = 1;
    wp::int32 var_98{};
    const wp::int32 var_99 = 2;
    wp::float32 var_100{};
    const wp::int32 var_101 = 2;
    wp::int32 var_102{};
    bool var_103{};
    const wp::int32 var_104 = 4;
    bool var_105{};
    const wp::int32 var_106 = 5;
    bool var_107{};
    const wp::int32 var_108 = 0;
    bool var_109{};
    const wp::int32 var_110 = 0;
    wp::int32 var_111{};
    wp::float32* var_112{};
    const wp::int32 var_113 = 1;
    wp::int32 var_114{};
    wp::float32* var_115{};
    const wp::int32 var_116 = 2;
    wp::int32 var_117{};
    wp::float32* var_118{};
    wp::vec_t<3, wp::float32> var_119{};
    wp::float32 var_120{};
    wp::float32 var_121{};
    wp::float32 var_122{};
    const wp::int32 var_123 = 3;
    wp::int32 var_124{};
    wp::float32* var_125{};
    const wp::int32 var_126 = 4;
    wp::int32 var_127{};
    wp::float32* var_128{};
    const wp::int32 var_129 = 5;
    wp::int32 var_130{};
    wp::float32* var_131{};
    wp::vec_t<3, wp::float32> var_132{};
    wp::float32 var_133{};
    wp::float32 var_134{};
    wp::float32 var_135{};
    const wp::int32 var_136 = 0;
    wp::int32 var_137{};
    wp::float32* var_138{};
    const wp::int32 var_139 = 1;
    wp::int32 var_140{};
    wp::float32* var_141{};
    const wp::int32 var_142 = 2;
    wp::int32 var_143{};
    wp::float32* var_144{};
    wp::vec_t<3, wp::float32> var_145{};
    wp::float32 var_146{};
    wp::float32 var_147{};
    wp::float32 var_148{};
    const wp::int32 var_149 = 3;
    wp::int32 var_150{};
    wp::float32* var_151{};
    const wp::int32 var_152 = 4;
    wp::int32 var_153{};
    wp::float32* var_154{};
    const wp::int32 var_155 = 5;
    wp::int32 var_156{};
    wp::float32* var_157{};
    wp::vec_t<3, wp::float32> var_158{};
    wp::float32 var_159{};
    wp::float32 var_160{};
    wp::float32 var_161{};
    const wp::int32 var_162 = 0;
    wp::int32 var_163{};
    wp::float32* var_164{};
    const wp::int32 var_165 = 1;
    wp::int32 var_166{};
    wp::float32* var_167{};
    const wp::int32 var_168 = 2;
    wp::int32 var_169{};
    wp::float32* var_170{};
    wp::vec_t<3, wp::float32> var_171{};
    wp::float32 var_172{};
    wp::float32 var_173{};
    wp::float32 var_174{};
    const wp::int32 var_175 = 3;
    wp::int32 var_176{};
    wp::float32* var_177{};
    const wp::int32 var_178 = 4;
    wp::int32 var_179{};
    wp::float32* var_180{};
    const wp::int32 var_181 = 5;
    wp::int32 var_182{};
    wp::float32* var_183{};
    const wp::int32 var_184 = 6;
    wp::int32 var_185{};
    wp::float32* var_186{};
    wp::quat_t<wp::float32> var_187{};
    wp::float32 var_188{};
    wp::float32 var_189{};
    wp::float32 var_190{};
    wp::float32 var_191{};
    wp::transform_t<wp::float32> var_192{};
    wp::vec_t<3, wp::float32> var_193{};
    wp::vec_t<3, wp::float32> var_194{};
    wp::vec_t<3, wp::float32> var_195{};
    wp::vec_t<3, wp::float32> var_196{};
    wp::vec_t<3, wp::float32> var_197{};
    wp::vec_t<3, wp::float32> var_198{};
    wp::vec_t<3, wp::float32> var_199{};
    wp::vec_t<3, wp::float32> var_200{};
    wp::vec_t<3, wp::float32> var_201{};
    wp::vec_t<3, wp::float32> var_202{};
    wp::vec_t<3, wp::float32> var_203{};
    wp::vec_t<3, wp::float32> var_204{};
    wp::vec_t<3, wp::float32> var_205{};
    const wp::float32 var_206 = 0.0;
    wp::quat_t<wp::float32> var_207{};
    wp::quat_t<wp::float32> var_208{};
    const wp::float32 var_209 = 0.5;
    wp::quat_t<wp::float32> var_210{};
    wp::quat_t<wp::float32> var_211{};
    wp::quat_t<wp::float32> var_212{};
    wp::quat_t<wp::float32> var_213{};
    wp::vec_t<3, wp::float32> var_214{};
    wp::vec_t<3, wp::float32> var_215{};
    wp::vec_t<3, wp::float32> var_216{};
    wp::vec_t<3, wp::float32> var_217{};
    wp::vec_t<3, wp::float32> var_218{};
    wp::vec_t<3, wp::float32> var_219{};
    const wp::int32 var_220 = 0;
    wp::float32 var_221{};
    const wp::int32 var_222 = 0;
    wp::int32 var_223{};
    const wp::int32 var_224 = 1;
    wp::float32 var_225{};
    const wp::int32 var_226 = 1;
    wp::int32 var_227{};
    const wp::int32 var_228 = 2;
    wp::float32 var_229{};
    const wp::int32 var_230 = 2;
    wp::int32 var_231{};
    const wp::int32 var_232 = 0;
    wp::float32 var_233{};
    const wp::int32 var_234 = 3;
    wp::int32 var_235{};
    const wp::int32 var_236 = 1;
    wp::float32 var_237{};
    const wp::int32 var_238 = 4;
    wp::int32 var_239{};
    const wp::int32 var_240 = 2;
    wp::float32 var_241{};
    const wp::int32 var_242 = 5;
    wp::int32 var_243{};
    const wp::int32 var_244 = 3;
    wp::float32 var_245{};
    const wp::int32 var_246 = 6;
    wp::int32 var_247{};
    const wp::int32 var_248 = 0;
    wp::float32 var_249{};
    const wp::int32 var_250 = 0;
    wp::int32 var_251{};
    const wp::int32 var_252 = 1;
    wp::float32 var_253{};
    const wp::int32 var_254 = 1;
    wp::int32 var_255{};
    const wp::int32 var_256 = 2;
    wp::float32 var_257{};
    const wp::int32 var_258 = 2;
    wp::int32 var_259{};
    const wp::int32 var_260 = 0;
    wp::float32 var_261{};
    const wp::int32 var_262 = 3;
    wp::int32 var_263{};
    const wp::int32 var_264 = 1;
    wp::float32 var_265{};
    const wp::int32 var_266 = 4;
    wp::int32 var_267{};
    const wp::int32 var_268 = 2;
    wp::float32 var_269{};
    const wp::int32 var_270 = 5;
    wp::int32 var_271{};
    const wp::int32 var_272 = 0;
    wp::int32 var_273{};
    wp::float32* var_274{};
    const wp::int32 var_275 = 1;
    wp::int32 var_276{};
    wp::float32* var_277{};
    const wp::int32 var_278 = 2;
    wp::int32 var_279{};
    wp::float32* var_280{};
    wp::vec_t<3, wp::float32> var_281{};
    wp::float32 var_282{};
    wp::float32 var_283{};
    wp::float32 var_284{};
    const wp::int32 var_285 = 3;
    wp::int32 var_286{};
    wp::float32* var_287{};
    const wp::int32 var_288 = 4;
    wp::int32 var_289{};
    wp::float32* var_290{};
    const wp::int32 var_291 = 5;
    wp::int32 var_292{};
    wp::float32* var_293{};
    wp::vec_t<3, wp::float32> var_294{};
    wp::float32 var_295{};
    wp::float32 var_296{};
    wp::float32 var_297{};
    const wp::int32 var_298 = 0;
    wp::int32 var_299{};
    wp::float32* var_300{};
    const wp::int32 var_301 = 1;
    wp::int32 var_302{};
    wp::float32* var_303{};
    const wp::int32 var_304 = 2;
    wp::int32 var_305{};
    wp::float32* var_306{};
    wp::vec_t<3, wp::float32> var_307{};
    wp::float32 var_308{};
    wp::float32 var_309{};
    wp::float32 var_310{};
    const wp::int32 var_311 = 3;
    wp::int32 var_312{};
    wp::float32* var_313{};
    const wp::int32 var_314 = 4;
    wp::int32 var_315{};
    wp::float32* var_316{};
    const wp::int32 var_317 = 5;
    wp::int32 var_318{};
    wp::float32* var_319{};
    wp::vec_t<3, wp::float32> var_320{};
    wp::float32 var_321{};
    wp::float32 var_322{};
    wp::float32 var_323{};
    wp::vec_t<3, wp::float32> var_324{};
    wp::vec_t<3, wp::float32> var_325{};
    wp::vec_t<3, wp::float32> var_326{};
    wp::vec_t<3, wp::float32> var_327{};
    const wp::int32 var_328 = 0;
    wp::int32 var_329{};
    wp::float32* var_330{};
    const wp::int32 var_331 = 1;
    wp::int32 var_332{};
    wp::float32* var_333{};
    const wp::int32 var_334 = 2;
    wp::int32 var_335{};
    wp::float32* var_336{};
    wp::vec_t<3, wp::float32> var_337{};
    wp::float32 var_338{};
    wp::float32 var_339{};
    wp::float32 var_340{};
    wp::vec_t<3, wp::float32> var_341{};
    wp::vec_t<3, wp::float32> var_342{};
    const wp::int32 var_343 = 3;
    wp::int32 var_344{};
    wp::float32* var_345{};
    const wp::int32 var_346 = 4;
    wp::int32 var_347{};
    wp::float32* var_348{};
    const wp::int32 var_349 = 5;
    wp::int32 var_350{};
    wp::float32* var_351{};
    const wp::int32 var_352 = 6;
    wp::int32 var_353{};
    wp::float32* var_354{};
    wp::quat_t<wp::float32> var_355{};
    wp::float32 var_356{};
    wp::float32 var_357{};
    wp::float32 var_358{};
    wp::float32 var_359{};
    const wp::float32 var_360 = 0.0;
    wp::quat_t<wp::float32> var_361{};
    wp::quat_t<wp::float32> var_362{};
    const wp::float32 var_363 = 0.5;
    wp::quat_t<wp::float32> var_364{};
    wp::vec_t<3, wp::float32> var_365{};
    wp::vec_t<3, wp::float32> var_366{};
    wp::quat_t<wp::float32> var_367{};
    wp::quat_t<wp::float32> var_368{};
    wp::quat_t<wp::float32> var_369{};
    const wp::int32 var_370 = 0;
    wp::float32 var_371{};
    const wp::int32 var_372 = 0;
    wp::int32 var_373{};
    const wp::int32 var_374 = 1;
    wp::float32 var_375{};
    const wp::int32 var_376 = 1;
    wp::int32 var_377{};
    const wp::int32 var_378 = 2;
    wp::float32 var_379{};
    const wp::int32 var_380 = 2;
    wp::int32 var_381{};
    const wp::int32 var_382 = 0;
    wp::float32 var_383{};
    const wp::int32 var_384 = 3;
    wp::int32 var_385{};
    const wp::int32 var_386 = 1;
    wp::float32 var_387{};
    const wp::int32 var_388 = 4;
    wp::int32 var_389{};
    const wp::int32 var_390 = 2;
    wp::float32 var_391{};
    const wp::int32 var_392 = 5;
    wp::int32 var_393{};
    const wp::int32 var_394 = 3;
    wp::float32 var_395{};
    const wp::int32 var_396 = 6;
    wp::int32 var_397{};
    const wp::int32 var_398 = 0;
    wp::float32 var_399{};
    const wp::int32 var_400 = 0;
    wp::int32 var_401{};
    const wp::int32 var_402 = 1;
    wp::float32 var_403{};
    const wp::int32 var_404 = 1;
    wp::int32 var_405{};
    const wp::int32 var_406 = 2;
    wp::float32 var_407{};
    const wp::int32 var_408 = 2;
    wp::int32 var_409{};
    const wp::int32 var_410 = 0;
    wp::float32 var_411{};
    const wp::int32 var_412 = 3;
    wp::int32 var_413{};
    const wp::int32 var_414 = 1;
    wp::float32 var_415{};
    const wp::int32 var_416 = 4;
    wp::int32 var_417{};
    const wp::int32 var_418 = 2;
    wp::float32 var_419{};
    const wp::int32 var_420 = 5;
    wp::int32 var_421{};
    const wp::int32 var_422 = 6;
    bool var_423{};
    wp::int32 var_424{};
    wp::range_t var_425{};
    wp::int32 var_426{};
    wp::int32 var_427{};
    wp::float32* var_428{};
    wp::float32 var_429{};
    wp::float32 var_430{};
    wp::int32 var_431{};
    wp::float32* var_432{};
    wp::float32 var_433{};
    wp::float32 var_434{};
    wp::int32 var_435{};
    wp::float32* var_436{};
    wp::float32 var_437{};
    wp::float32 var_438{};
    wp::float32 var_439{};
    wp::float32 var_440{};
    wp::float32 var_441{};
    wp::float32 var_442{};
    wp::int32 var_443{};
    wp::int32 var_444{};
    //---------
    // forward
    // def jcalc_integrate(                                                                   <L 585>
    // if type == JointType.FIXED:                                                            <L 602>
    var_1 = (var_type == var_0);
    if (var_1) {
        // return                                                                             <L 603>
        return;
    }
    // if type == JointType.PRISMATIC or type == JointType.REVOLUTE:                          <L 606>
    var_4 = (var_type == var_3);
    var_2 = var_4;
    if (!var_2) {
        var_6 = (var_type == var_5);
        var_2 = var_2 || var_6;
    }
    if (var_2) {
        // qdd = joint_qdd[dof_start]                                                         <L 607>
        var_7 = wp::address(var_joint_qdd, var_dof_start);
        var_9 = wp::load(var_7);
        var_8 = wp::copy(var_9);
        // qd = joint_qd[dof_start]                                                           <L 608>
        var_10 = wp::address(var_joint_qd, var_dof_start);
        var_12 = wp::load(var_10);
        var_11 = wp::copy(var_12);
        // q = joint_q[coord_start]                                                           <L 609>
        var_13 = wp::address(var_joint_q, var_coord_start);
        var_15 = wp::load(var_13);
        var_14 = wp::copy(var_15);
        // qd_new = qd + qdd * dt                                                             <L 611>
        var_16 = wp::mul(var_8, var_dt);
        var_17 = wp::add(var_11, var_16);
        // q_new = q + qd_new * dt                                                            <L 612>
        var_18 = wp::mul(var_17, var_dt);
        var_19 = wp::add(var_14, var_18);
        // joint_qd_new[dof_start] = qd_new                                                   <L 614>
        wp::array_store(var_joint_qd_new, var_dof_start, var_17);
        // joint_q_new[coord_start] = q_new                                                   <L 615>
        wp::array_store(var_joint_q_new, var_coord_start, var_19);
        // return                                                                             <L 617>
        return;
    }
    // if type == JointType.BALL:                                                             <L 620>
    var_21 = (var_type == var_20);
    if (var_21) {
        // m_j = wp.vec3(joint_qdd[dof_start + 0], joint_qdd[dof_start + 1], joint_qdd[dof_start + 2])       <L 621>
        var_23 = wp::add(var_dof_start, var_22);
        var_24 = wp::address(var_joint_qdd, var_23);
        var_26 = wp::add(var_dof_start, var_25);
        var_27 = wp::address(var_joint_qdd, var_26);
        var_29 = wp::add(var_dof_start, var_28);
        var_30 = wp::address(var_joint_qdd, var_29);
        var_32 = wp::load(var_24);
        var_33 = wp::load(var_27);
        var_34 = wp::load(var_30);
        var_31 = wp::vec_t<3, wp::float32>(var_32, var_33, var_34);
        // w_j = wp.vec3(joint_qd[dof_start + 0], joint_qd[dof_start + 1], joint_qd[dof_start + 2])       <L 622>
        var_36 = wp::add(var_dof_start, var_35);
        var_37 = wp::address(var_joint_qd, var_36);
        var_39 = wp::add(var_dof_start, var_38);
        var_40 = wp::address(var_joint_qd, var_39);
        var_42 = wp::add(var_dof_start, var_41);
        var_43 = wp::address(var_joint_qd, var_42);
        var_45 = wp::load(var_37);
        var_46 = wp::load(var_40);
        var_47 = wp::load(var_43);
        var_44 = wp::vec_t<3, wp::float32>(var_45, var_46, var_47);
        // r_j = wp.quat(                                                                     <L 624>
        // joint_q[coord_start + 0], joint_q[coord_start + 1], joint_q[coord_start + 2], joint_q[coord_start + 3]       <L 625>
        var_49 = wp::add(var_coord_start, var_48);
        var_50 = wp::address(var_joint_q, var_49);
        var_52 = wp::add(var_coord_start, var_51);
        var_53 = wp::address(var_joint_q, var_52);
        var_55 = wp::add(var_coord_start, var_54);
        var_56 = wp::address(var_joint_q, var_55);
        var_58 = wp::add(var_coord_start, var_57);
        var_59 = wp::address(var_joint_q, var_58);
        var_61 = wp::load(var_50);
        var_62 = wp::load(var_53);
        var_63 = wp::load(var_56);
        var_64 = wp::load(var_59);
        var_60 = wp::quat_t<wp::float32>(var_61, var_62, var_63, var_64);
        // w_j_new = w_j + m_j * dt                                                           <L 629>
        var_65 = wp::mul(var_31, var_dt);
        var_66 = wp::add(var_44, var_65);
        // drdt_j = wp.quat(w_j_new, 0.0) * r_j * 0.5                                         <L 631>
        var_68 = wp::quat_t<wp::float32>(var_66, var_67);
        var_69 = wp::mul(var_68, var_60);
        var_71 = wp::mul(var_69, var_70);
        // r_j_new = wp.normalize(r_j + drdt_j * dt)                                          <L 634>
        var_72 = wp::mul(var_71, var_dt);
        var_73 = wp::add(var_60, var_72);
        var_74 = wp::normalize(var_73);
        // joint_q_new[coord_start + 0] = r_j_new[0]                                          <L 637>
        var_76 = wp::extract(var_74, var_75);
        var_78 = wp::add(var_coord_start, var_77);
        wp::array_store(var_joint_q_new, var_78, var_76);
        // joint_q_new[coord_start + 1] = r_j_new[1]                                          <L 638>
        var_80 = wp::extract(var_74, var_79);
        var_82 = wp::add(var_coord_start, var_81);
        wp::array_store(var_joint_q_new, var_82, var_80);
        // joint_q_new[coord_start + 2] = r_j_new[2]                                          <L 639>
        var_84 = wp::extract(var_74, var_83);
        var_86 = wp::add(var_coord_start, var_85);
        wp::array_store(var_joint_q_new, var_86, var_84);
        // joint_q_new[coord_start + 3] = r_j_new[3]                                          <L 640>
        var_88 = wp::extract(var_74, var_87);
        var_90 = wp::add(var_coord_start, var_89);
        wp::array_store(var_joint_q_new, var_90, var_88);
        // joint_qd_new[dof_start + 0] = w_j_new[0]                                           <L 643>
        var_92 = wp::extract(var_66, var_91);
        var_94 = wp::add(var_dof_start, var_93);
        wp::array_store(var_joint_qd_new, var_94, var_92);
        // joint_qd_new[dof_start + 1] = w_j_new[1]                                           <L 644>
        var_96 = wp::extract(var_66, var_95);
        var_98 = wp::add(var_dof_start, var_97);
        wp::array_store(var_joint_qd_new, var_98, var_96);
        // joint_qd_new[dof_start + 2] = w_j_new[2]                                           <L 645>
        var_100 = wp::extract(var_66, var_99);
        var_102 = wp::add(var_dof_start, var_101);
        wp::array_store(var_joint_qd_new, var_102, var_100);
        // return                                                                             <L 647>
        return;
    }
    // if type == JointType.FREE or type == JointType.DISTANCE:                               <L 649>
    var_105 = (var_type == var_104);
    var_103 = var_105;
    if (!var_103) {
        var_107 = (var_type == var_106);
        var_103 = var_103 || var_107;
    }
    if (var_103) {
        // if parent < 0:                                                                     <L 650>
        var_109 = (var_parent < var_108);
        if (var_109) {
            // a_parent = wp.vec3(joint_qdd[dof_start + 0], joint_qdd[dof_start + 1], joint_qdd[dof_start + 2])       <L 651>
            var_111 = wp::add(var_dof_start, var_110);
            var_112 = wp::address(var_joint_qdd, var_111);
            var_114 = wp::add(var_dof_start, var_113);
            var_115 = wp::address(var_joint_qdd, var_114);
            var_117 = wp::add(var_dof_start, var_116);
            var_118 = wp::address(var_joint_qdd, var_117);
            var_120 = wp::load(var_112);
            var_121 = wp::load(var_115);
            var_122 = wp::load(var_118);
            var_119 = wp::vec_t<3, wp::float32>(var_120, var_121, var_122);
            // alpha = wp.vec3(joint_qdd[dof_start + 3], joint_qdd[dof_start + 4], joint_qdd[dof_start + 5])       <L 652>
            var_124 = wp::add(var_dof_start, var_123);
            var_125 = wp::address(var_joint_qdd, var_124);
            var_127 = wp::add(var_dof_start, var_126);
            var_128 = wp::address(var_joint_qdd, var_127);
            var_130 = wp::add(var_dof_start, var_129);
            var_131 = wp::address(var_joint_qdd, var_130);
            var_133 = wp::load(var_125);
            var_134 = wp::load(var_128);
            var_135 = wp::load(var_131);
            var_132 = wp::vec_t<3, wp::float32>(var_133, var_134, var_135);
            // v_parent = wp.vec3(joint_qd[dof_start + 0], joint_qd[dof_start + 1], joint_qd[dof_start + 2])       <L 654>
            var_137 = wp::add(var_dof_start, var_136);
            var_138 = wp::address(var_joint_qd, var_137);
            var_140 = wp::add(var_dof_start, var_139);
            var_141 = wp::address(var_joint_qd, var_140);
            var_143 = wp::add(var_dof_start, var_142);
            var_144 = wp::address(var_joint_qd, var_143);
            var_146 = wp::load(var_138);
            var_147 = wp::load(var_141);
            var_148 = wp::load(var_144);
            var_145 = wp::vec_t<3, wp::float32>(var_146, var_147, var_148);
            // omega = wp.vec3(joint_qd[dof_start + 3], joint_qd[dof_start + 4], joint_qd[dof_start + 5])       <L 655>
            var_150 = wp::add(var_dof_start, var_149);
            var_151 = wp::address(var_joint_qd, var_150);
            var_153 = wp::add(var_dof_start, var_152);
            var_154 = wp::address(var_joint_qd, var_153);
            var_156 = wp::add(var_dof_start, var_155);
            var_157 = wp::address(var_joint_qd, var_156);
            var_159 = wp::load(var_151);
            var_160 = wp::load(var_154);
            var_161 = wp::load(var_157);
            var_158 = wp::vec_t<3, wp::float32>(var_159, var_160, var_161);
            // p = wp.vec3(joint_q[coord_start + 0], joint_q[coord_start + 1], joint_q[coord_start + 2])       <L 657>
            var_163 = wp::add(var_coord_start, var_162);
            var_164 = wp::address(var_joint_q, var_163);
            var_166 = wp::add(var_coord_start, var_165);
            var_167 = wp::address(var_joint_q, var_166);
            var_169 = wp::add(var_coord_start, var_168);
            var_170 = wp::address(var_joint_q, var_169);
            var_172 = wp::load(var_164);
            var_173 = wp::load(var_167);
            var_174 = wp::load(var_170);
            var_171 = wp::vec_t<3, wp::float32>(var_172, var_173, var_174);
            // r = wp.quat(                                                                   <L 658>
            // joint_q[coord_start + 3], joint_q[coord_start + 4], joint_q[coord_start + 5], joint_q[coord_start + 6]       <L 659>
            var_176 = wp::add(var_coord_start, var_175);
            var_177 = wp::address(var_joint_q, var_176);
            var_179 = wp::add(var_coord_start, var_178);
            var_180 = wp::address(var_joint_q, var_179);
            var_182 = wp::add(var_coord_start, var_181);
            var_183 = wp::address(var_joint_q, var_182);
            var_185 = wp::add(var_coord_start, var_184);
            var_186 = wp::address(var_joint_q, var_185);
            var_188 = wp::load(var_177);
            var_189 = wp::load(var_180);
            var_190 = wp::load(var_183);
            var_191 = wp::load(var_186);
            var_187 = wp::quat_t<wp::float32>(var_188, var_189, var_190, var_191);
            // r_com_joint = wp.transform_point(wp.transform_inverse(joint_X_c), body_com_child)       <L 662>
            var_192 = wp::transform_inverse(var_joint_X_c);
            var_193 = wp::transform_point(var_192, var_body_com_child);
            // x_com = p + wp.quat_rotate(r, r_com_joint)                                     <L 663>
            var_194 = wp::quat_rotate(var_187, var_193);
            var_195 = wp::add(var_171, var_194);
            // v_com = v_parent + wp.cross(omega, x_com)                                      <L 664>
            var_196 = wp::cross(var_158, var_195);
            var_197 = wp::add(var_145, var_196);
            // a_com = a_parent + wp.cross(alpha, x_com) + wp.cross(omega, v_com)             <L 665>
            var_198 = wp::cross(var_132, var_195);
            var_199 = wp::add(var_119, var_198);
            var_200 = wp::cross(var_158, var_197);
            var_201 = wp::add(var_199, var_200);
            // omega_new = omega + alpha * dt                                                 <L 667>
            var_202 = wp::mul(var_132, var_dt);
            var_203 = wp::add(var_158, var_202);
            // v_com_new = v_com + a_com * dt                                                 <L 668>
            var_204 = wp::mul(var_201, var_dt);
            var_205 = wp::add(var_197, var_204);
            // drdt = wp.quat(omega_new, 0.0) * r * 0.5                                       <L 670>
            var_207 = wp::quat_t<wp::float32>(var_203, var_206);
            var_208 = wp::mul(var_207, var_187);
            var_210 = wp::mul(var_208, var_209);
            // r_new = wp.normalize(r + drdt * dt)                                            <L 671>
            var_211 = wp::mul(var_210, var_dt);
            var_212 = wp::add(var_187, var_211);
            var_213 = wp::normalize(var_212);
            // x_com_new = x_com + v_com_new * dt                                             <L 672>
            var_214 = wp::mul(var_205, var_dt);
            var_215 = wp::add(var_195, var_214);
            // p_new = x_com_new - wp.quat_rotate(r_new, r_com_joint)                         <L 673>
            var_216 = wp::quat_rotate(var_213, var_193);
            var_217 = wp::sub(var_215, var_216);
            // v_parent_new = v_com_new - wp.cross(omega_new, x_com_new)                      <L 674>
            var_218 = wp::cross(var_203, var_215);
            var_219 = wp::sub(var_205, var_218);
            // joint_q_new[coord_start + 0] = p_new[0]                                        <L 676>
            var_221 = wp::extract(var_217, var_220);
            var_223 = wp::add(var_coord_start, var_222);
            wp::array_store(var_joint_q_new, var_223, var_221);
            // joint_q_new[coord_start + 1] = p_new[1]                                        <L 677>
            var_225 = wp::extract(var_217, var_224);
            var_227 = wp::add(var_coord_start, var_226);
            wp::array_store(var_joint_q_new, var_227, var_225);
            // joint_q_new[coord_start + 2] = p_new[2]                                        <L 678>
            var_229 = wp::extract(var_217, var_228);
            var_231 = wp::add(var_coord_start, var_230);
            wp::array_store(var_joint_q_new, var_231, var_229);
            // joint_q_new[coord_start + 3] = r_new[0]                                        <L 680>
            var_233 = wp::extract(var_213, var_232);
            var_235 = wp::add(var_coord_start, var_234);
            wp::array_store(var_joint_q_new, var_235, var_233);
            // joint_q_new[coord_start + 4] = r_new[1]                                        <L 681>
            var_237 = wp::extract(var_213, var_236);
            var_239 = wp::add(var_coord_start, var_238);
            wp::array_store(var_joint_q_new, var_239, var_237);
            // joint_q_new[coord_start + 5] = r_new[2]                                        <L 682>
            var_241 = wp::extract(var_213, var_240);
            var_243 = wp::add(var_coord_start, var_242);
            wp::array_store(var_joint_q_new, var_243, var_241);
            // joint_q_new[coord_start + 6] = r_new[3]                                        <L 683>
            var_245 = wp::extract(var_213, var_244);
            var_247 = wp::add(var_coord_start, var_246);
            wp::array_store(var_joint_q_new, var_247, var_245);
            // joint_qd_new[dof_start + 0] = v_parent_new[0]                                  <L 685>
            var_249 = wp::extract(var_219, var_248);
            var_251 = wp::add(var_dof_start, var_250);
            wp::array_store(var_joint_qd_new, var_251, var_249);
            // joint_qd_new[dof_start + 1] = v_parent_new[1]                                  <L 686>
            var_253 = wp::extract(var_219, var_252);
            var_255 = wp::add(var_dof_start, var_254);
            wp::array_store(var_joint_qd_new, var_255, var_253);
            // joint_qd_new[dof_start + 2] = v_parent_new[2]                                  <L 687>
            var_257 = wp::extract(var_219, var_256);
            var_259 = wp::add(var_dof_start, var_258);
            wp::array_store(var_joint_qd_new, var_259, var_257);
            // joint_qd_new[dof_start + 3] = omega_new[0]                                     <L 688>
            var_261 = wp::extract(var_203, var_260);
            var_263 = wp::add(var_dof_start, var_262);
            wp::array_store(var_joint_qd_new, var_263, var_261);
            // joint_qd_new[dof_start + 4] = omega_new[1]                                     <L 689>
            var_265 = wp::extract(var_203, var_264);
            var_267 = wp::add(var_dof_start, var_266);
            wp::array_store(var_joint_qd_new, var_267, var_265);
            // joint_qd_new[dof_start + 5] = omega_new[2]                                     <L 690>
            var_269 = wp::extract(var_203, var_268);
            var_271 = wp::add(var_dof_start, var_270);
            wp::array_store(var_joint_qd_new, var_271, var_269);
            // return                                                                         <L 691>
            return;
        }
        // a_s = wp.vec3(joint_qdd[dof_start + 0], joint_qdd[dof_start + 1], joint_qdd[dof_start + 2])       <L 693>
        var_273 = wp::add(var_dof_start, var_272);
        var_274 = wp::address(var_joint_qdd, var_273);
        var_276 = wp::add(var_dof_start, var_275);
        var_277 = wp::address(var_joint_qdd, var_276);
        var_279 = wp::add(var_dof_start, var_278);
        var_280 = wp::address(var_joint_qdd, var_279);
        var_282 = wp::load(var_274);
        var_283 = wp::load(var_277);
        var_284 = wp::load(var_280);
        var_281 = wp::vec_t<3, wp::float32>(var_282, var_283, var_284);
        // m_s = wp.vec3(joint_qdd[dof_start + 3], joint_qdd[dof_start + 4], joint_qdd[dof_start + 5])       <L 694>
        var_286 = wp::add(var_dof_start, var_285);
        var_287 = wp::address(var_joint_qdd, var_286);
        var_289 = wp::add(var_dof_start, var_288);
        var_290 = wp::address(var_joint_qdd, var_289);
        var_292 = wp::add(var_dof_start, var_291);
        var_293 = wp::address(var_joint_qdd, var_292);
        var_295 = wp::load(var_287);
        var_296 = wp::load(var_290);
        var_297 = wp::load(var_293);
        var_294 = wp::vec_t<3, wp::float32>(var_295, var_296, var_297);
        // v_s = wp.vec3(joint_qd[dof_start + 0], joint_qd[dof_start + 1], joint_qd[dof_start + 2])       <L 696>
        var_299 = wp::add(var_dof_start, var_298);
        var_300 = wp::address(var_joint_qd, var_299);
        var_302 = wp::add(var_dof_start, var_301);
        var_303 = wp::address(var_joint_qd, var_302);
        var_305 = wp::add(var_dof_start, var_304);
        var_306 = wp::address(var_joint_qd, var_305);
        var_308 = wp::load(var_300);
        var_309 = wp::load(var_303);
        var_310 = wp::load(var_306);
        var_307 = wp::vec_t<3, wp::float32>(var_308, var_309, var_310);
        // w_s = wp.vec3(joint_qd[dof_start + 3], joint_qd[dof_start + 4], joint_qd[dof_start + 5])       <L 697>
        var_312 = wp::add(var_dof_start, var_311);
        var_313 = wp::address(var_joint_qd, var_312);
        var_315 = wp::add(var_dof_start, var_314);
        var_316 = wp::address(var_joint_qd, var_315);
        var_318 = wp::add(var_dof_start, var_317);
        var_319 = wp::address(var_joint_qd, var_318);
        var_321 = wp::load(var_313);
        var_322 = wp::load(var_316);
        var_323 = wp::load(var_319);
        var_320 = wp::vec_t<3, wp::float32>(var_321, var_322, var_323);
        // w_s = w_s + m_s * dt                                                               <L 702>
        var_324 = wp::mul(var_294, var_dt);
        var_325 = wp::add(var_320, var_324);
        // v_s = v_s + a_s * dt                                                               <L 703>
        var_326 = wp::mul(var_281, var_dt);
        var_327 = wp::add(var_307, var_326);
        // p_s = wp.vec3(joint_q[coord_start + 0], joint_q[coord_start + 1], joint_q[coord_start + 2])       <L 705>
        var_329 = wp::add(var_coord_start, var_328);
        var_330 = wp::address(var_joint_q, var_329);
        var_332 = wp::add(var_coord_start, var_331);
        var_333 = wp::address(var_joint_q, var_332);
        var_335 = wp::add(var_coord_start, var_334);
        var_336 = wp::address(var_joint_q, var_335);
        var_338 = wp::load(var_330);
        var_339 = wp::load(var_333);
        var_340 = wp::load(var_336);
        var_337 = wp::vec_t<3, wp::float32>(var_338, var_339, var_340);
        // dpdt_s = v_s + wp.cross(w_s, p_s)                                                  <L 707>
        var_341 = wp::cross(var_325, var_337);
        var_342 = wp::add(var_327, var_341);
        // r_s = wp.quat(                                                                     <L 708>
        // joint_q[coord_start + 3], joint_q[coord_start + 4], joint_q[coord_start + 5], joint_q[coord_start + 6]       <L 709>
        var_344 = wp::add(var_coord_start, var_343);
        var_345 = wp::address(var_joint_q, var_344);
        var_347 = wp::add(var_coord_start, var_346);
        var_348 = wp::address(var_joint_q, var_347);
        var_350 = wp::add(var_coord_start, var_349);
        var_351 = wp::address(var_joint_q, var_350);
        var_353 = wp::add(var_coord_start, var_352);
        var_354 = wp::address(var_joint_q, var_353);
        var_356 = wp::load(var_345);
        var_357 = wp::load(var_348);
        var_358 = wp::load(var_351);
        var_359 = wp::load(var_354);
        var_355 = wp::quat_t<wp::float32>(var_356, var_357, var_358, var_359);
        // drdt_s = wp.quat(w_s, 0.0) * r_s * 0.5                                             <L 712>
        var_361 = wp::quat_t<wp::float32>(var_325, var_360);
        var_362 = wp::mul(var_361, var_355);
        var_364 = wp::mul(var_362, var_363);
        // p_s_new = p_s + dpdt_s * dt                                                        <L 714>
        var_365 = wp::mul(var_342, var_dt);
        var_366 = wp::add(var_337, var_365);
        // r_s_new = wp.normalize(r_s + drdt_s * dt)                                          <L 715>
        var_367 = wp::mul(var_364, var_dt);
        var_368 = wp::add(var_355, var_367);
        var_369 = wp::normalize(var_368);
        // joint_q_new[coord_start + 0] = p_s_new[0]                                          <L 717>
        var_371 = wp::extract(var_366, var_370);
        var_373 = wp::add(var_coord_start, var_372);
        wp::array_store(var_joint_q_new, var_373, var_371);
        // joint_q_new[coord_start + 1] = p_s_new[1]                                          <L 718>
        var_375 = wp::extract(var_366, var_374);
        var_377 = wp::add(var_coord_start, var_376);
        wp::array_store(var_joint_q_new, var_377, var_375);
        // joint_q_new[coord_start + 2] = p_s_new[2]                                          <L 719>
        var_379 = wp::extract(var_366, var_378);
        var_381 = wp::add(var_coord_start, var_380);
        wp::array_store(var_joint_q_new, var_381, var_379);
        // joint_q_new[coord_start + 3] = r_s_new[0]                                          <L 721>
        var_383 = wp::extract(var_369, var_382);
        var_385 = wp::add(var_coord_start, var_384);
        wp::array_store(var_joint_q_new, var_385, var_383);
        // joint_q_new[coord_start + 4] = r_s_new[1]                                          <L 722>
        var_387 = wp::extract(var_369, var_386);
        var_389 = wp::add(var_coord_start, var_388);
        wp::array_store(var_joint_q_new, var_389, var_387);
        // joint_q_new[coord_start + 5] = r_s_new[2]                                          <L 723>
        var_391 = wp::extract(var_369, var_390);
        var_393 = wp::add(var_coord_start, var_392);
        wp::array_store(var_joint_q_new, var_393, var_391);
        // joint_q_new[coord_start + 6] = r_s_new[3]                                          <L 724>
        var_395 = wp::extract(var_369, var_394);
        var_397 = wp::add(var_coord_start, var_396);
        wp::array_store(var_joint_q_new, var_397, var_395);
        // joint_qd_new[dof_start + 0] = v_s[0]                                               <L 726>
        var_399 = wp::extract(var_327, var_398);
        var_401 = wp::add(var_dof_start, var_400);
        wp::array_store(var_joint_qd_new, var_401, var_399);
        // joint_qd_new[dof_start + 1] = v_s[1]                                               <L 727>
        var_403 = wp::extract(var_327, var_402);
        var_405 = wp::add(var_dof_start, var_404);
        wp::array_store(var_joint_qd_new, var_405, var_403);
        // joint_qd_new[dof_start + 2] = v_s[2]                                               <L 728>
        var_407 = wp::extract(var_327, var_406);
        var_409 = wp::add(var_dof_start, var_408);
        wp::array_store(var_joint_qd_new, var_409, var_407);
        // joint_qd_new[dof_start + 3] = w_s[0]                                               <L 729>
        var_411 = wp::extract(var_325, var_410);
        var_413 = wp::add(var_dof_start, var_412);
        wp::array_store(var_joint_qd_new, var_413, var_411);
        // joint_qd_new[dof_start + 4] = w_s[1]                                               <L 730>
        var_415 = wp::extract(var_325, var_414);
        var_417 = wp::add(var_dof_start, var_416);
        wp::array_store(var_joint_qd_new, var_417, var_415);
        // joint_qd_new[dof_start + 5] = w_s[2]                                               <L 731>
        var_419 = wp::extract(var_325, var_418);
        var_421 = wp::add(var_dof_start, var_420);
        wp::array_store(var_joint_qd_new, var_421, var_419);
        // return                                                                             <L 733>
        return;
    }
    // if type == JointType.D6:                                                               <L 736>
    var_423 = (var_type == var_422);
    if (var_423) {
        // axis_count = lin_axis_count + ang_axis_count                                       <L 737>
        var_424 = wp::add(var_lin_axis_count, var_ang_axis_count);
        // for i in range(axis_count):                                                        <L 739>
        var_425 = wp::range(var_424);
        start_for_5:;
            if (iter_cmp(var_425) == 0) goto end_for_5;
            var_426 = wp::iter_next(var_425);
            // qdd = joint_qdd[dof_start + i]                                                 <L 740>
            var_427 = wp::add(var_dof_start, var_426);
            var_428 = wp::address(var_joint_qdd, var_427);
            var_430 = wp::load(var_428);
            var_429 = wp::copy(var_430);
            // qd = joint_qd[dof_start + i]                                                   <L 741>
            var_431 = wp::add(var_dof_start, var_426);
            var_432 = wp::address(var_joint_qd, var_431);
            var_434 = wp::load(var_432);
            var_433 = wp::copy(var_434);
            // q = joint_q[coord_start + i]                                                   <L 742>
            var_435 = wp::add(var_coord_start, var_426);
            var_436 = wp::address(var_joint_q, var_435);
            var_438 = wp::load(var_436);
            var_437 = wp::copy(var_438);
            // qd_new = qd + qdd * dt                                                         <L 744>
            var_439 = wp::mul(var_429, var_dt);
            var_440 = wp::add(var_433, var_439);
            // q_new = q + qd_new * dt                                                        <L 745>
            var_441 = wp::mul(var_440, var_dt);
            var_442 = wp::add(var_437, var_441);
            // joint_qd_new[dof_start + i] = qd_new                                           <L 747>
            var_443 = wp::add(var_dof_start, var_426);
            wp::array_store(var_joint_qd_new, var_443, var_440);
            // joint_q_new[coord_start + i] = q_new                                           <L 748>
            var_444 = wp::add(var_coord_start, var_426);
            wp::array_store(var_joint_q_new, var_444, var_442);
            wp::assign(var_8, var_429);
            wp::assign(var_11, var_433);
            wp::assign(var_14, var_437);
            wp::assign(var_17, var_440);
            wp::assign(var_19, var_442);
            goto start_for_5;
        end_for_5:;
        // return                                                                             <L 750>
        return;
    }
}


// newton/_src/solvers/featherstone/kernels.py:879
static wp::int32 dense_index_0(
    wp::int32 var_stride,
    wp::int32 var_i,
    wp::int32 var_j)
{
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32 var_1{};
    //---------
    // forward
    // def dense_index(stride: int, i: int, j: int):                                          <L 880>
    // return i * stride + j                                                                  <L 881>
    var_0 = wp::mul(var_i, var_stride);
    var_1 = wp::add(var_0, var_j);
    return var_1;
}


// newton/_src/solvers/featherstone/kernels.py:1763
static void dense_subs_0(
    wp::int32 var_n,
    wp::int32 var_L_start,
    wp::int32 var_b_start,
    wp::array_t<wp::float32> var_L,
    wp::array_t<wp::float32> var_b,
    wp::array_t<wp::float32> var_x)
{
    //---------
    // primal vars
    wp::range_t var_0{};
    wp::int32 var_1{};
    wp::int32 var_2{};
    wp::float32* var_3{};
    wp::float32 var_4{};
    wp::float32 var_5{};
    wp::range_t var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::float32* var_10{};
    wp::int32 var_11{};
    wp::float32* var_12{};
    wp::float32 var_13{};
    wp::float32 var_14{};
    wp::float32 var_15{};
    wp::float32 var_16{};
    wp::int32 var_17{};
    wp::int32 var_18{};
    wp::float32* var_19{};
    wp::float32 var_20{};
    wp::float32 var_21{};
    wp::int32 var_22{};
    const wp::int32 var_23 = 1;
    wp::int32 var_24{};
    const wp::int32 var_25 = -1;
    const wp::int32 var_26 = -1;
    wp::range_t var_27{};
    wp::int32 var_28{};
    wp::int32 var_29{};
    wp::float32* var_30{};
    wp::float32 var_31{};
    wp::float32 var_32{};
    const wp::int32 var_33 = 1;
    wp::int32 var_34{};
    wp::range_t var_35{};
    wp::int32 var_36{};
    wp::int32 var_37{};
    wp::int32 var_38{};
    wp::float32* var_39{};
    wp::int32 var_40{};
    wp::float32* var_41{};
    wp::float32 var_42{};
    wp::float32 var_43{};
    wp::float32 var_44{};
    wp::float32 var_45{};
    wp::int32 var_46{};
    wp::int32 var_47{};
    wp::float32* var_48{};
    wp::float32 var_49{};
    wp::float32 var_50{};
    wp::int32 var_51{};
    //---------
    // forward
    // def dense_subs(                                                                        <L 1764>
    // for i in range(n):                                                                     <L 1775>
    var_0 = wp::range(var_n);
    start_for_0:;
        if (iter_cmp(var_0) == 0) goto end_for_0;
        var_1 = wp::iter_next(var_0);
        // s = b[b_start + i]                                                                 <L 1776>
        var_2 = wp::add(var_b_start, var_1);
        var_3 = wp::address(var_b, var_2);
        var_5 = wp::load(var_3);
        var_4 = wp::copy(var_5);
        // for j in range(i):                                                                 <L 1778>
        var_6 = wp::range(var_1);
        start_for_2:;
            if (iter_cmp(var_6) == 0) goto end_for_2;
            var_7 = wp::iter_next(var_6);
            // s -= L[L_start + dense_index(n, i, j)] * x[b_start + j]                        <L 1779>
            var_8 = dense_index_0(var_n, var_1, var_7);
            var_9 = wp::add(var_L_start, var_8);
            var_10 = wp::address(var_L, var_9);
            var_11 = wp::add(var_b_start, var_7);
            var_12 = wp::address(var_x, var_11);
            var_14 = wp::load(var_10);
            var_15 = wp::load(var_12);
            var_13 = wp::mul(var_14, var_15);
            var_16 = wp::sub(var_4, var_13);
            wp::assign(var_4, var_16);
            goto start_for_2;
        end_for_2:;
        // x[b_start + i] = s / L[L_start + dense_index(n, i, i)]                             <L 1781>
        var_17 = dense_index_0(var_n, var_1, var_1);
        var_18 = wp::add(var_L_start, var_17);
        var_19 = wp::address(var_L, var_18);
        var_21 = wp::load(var_19);
        var_20 = wp::div(var_4, var_21);
        var_22 = wp::add(var_b_start, var_1);
        wp::array_store(var_x, var_22, var_20);
        goto start_for_0;
    end_for_0:;
    // for i in range(n - 1, -1, -1):                                                         <L 1784>
    var_24 = wp::sub(var_n, var_23);
    var_27 = wp::range(var_24, var_25, var_26);
    start_for_4:;
        if (iter_cmp(var_27) == 0) goto end_for_4;
        var_28 = wp::iter_next(var_27);
        // s = x[b_start + i]                                                                 <L 1785>
        var_29 = wp::add(var_b_start, var_28);
        var_30 = wp::address(var_x, var_29);
        var_32 = wp::load(var_30);
        var_31 = wp::copy(var_32);
        // for j in range(i + 1, n):                                                          <L 1787>
        var_34 = wp::add(var_28, var_33);
        var_35 = wp::range(var_34, var_n);
        start_for_6:;
            if (iter_cmp(var_35) == 0) goto end_for_6;
            var_36 = wp::iter_next(var_35);
            // s -= L[L_start + dense_index(n, j, i)] * x[b_start + j]                        <L 1788>
            var_37 = dense_index_0(var_n, var_36, var_28);
            var_38 = wp::add(var_L_start, var_37);
            var_39 = wp::address(var_L, var_38);
            var_40 = wp::add(var_b_start, var_36);
            var_41 = wp::address(var_x, var_40);
            var_43 = wp::load(var_39);
            var_44 = wp::load(var_41);
            var_42 = wp::mul(var_43, var_44);
            var_45 = wp::sub(var_31, var_42);
            wp::assign(var_31, var_45);
            goto start_for_6;
        end_for_6:;
        // x[b_start + i] = s / L[L_start + dense_index(n, i, i)]                             <L 1790>
        var_46 = dense_index_0(var_n, var_28, var_28);
        var_47 = wp::add(var_L_start, var_46);
        var_48 = wp::address(var_L, var_47);
        var_50 = wp::load(var_48);
        var_49 = wp::div(var_31, var_50);
        var_51 = wp::add(var_b_start, var_28);
        wp::array_store(var_x, var_51, var_49);
        wp::assign(var_4, var_31);
        wp::assign(var_7, var_36);
        goto start_for_4;
    end_for_4:;
}


// newton/_src/solvers/featherstone/kernels.py:1793
static void dense_solve_0(
    wp::int32 var_n,
    wp::int32 var_L_start,
    wp::int32 var_b_start,
    wp::array_t<wp::float32> var_A,
    wp::array_t<wp::float32> var_L,
    wp::array_t<wp::float32> var_b,
    wp::array_t<wp::float32> var_x,
    wp::array_t<wp::float32> var_tmp)
{
    //---------
    // primal vars
    //---------
    // forward
    // def dense_solve(                                                                       <L 1794>
    // dense_subs(n, L_start, b_start, L, b, x)                                               <L 1806>
    dense_subs_0(var_n, var_L_start, var_b_start, var_L, var_b, var_x);
}


// newton/_src/sim/articulation.py:39
static void transform_2d_rotational_axes_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::float32 var_q0,
    wp::vec_t<3, wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::mat_t<3, 3, wp::float32> var_1{};
    wp::quat_t<wp::float32> var_2{};
    const wp::float32 var_3 = 1.0;
    const wp::float32 var_4 = 0.0;
    const wp::float32 var_5 = 0.0;
    wp::vec_t<3, wp::float32> var_6{};
    wp::vec_t<3, wp::float32> var_7{};
    const wp::float32 var_8 = 0.0;
    const wp::float32 var_9 = 1.0;
    const wp::float32 var_10 = 0.0;
    wp::vec_t<3, wp::float32> var_11{};
    wp::vec_t<3, wp::float32> var_12{};
    wp::vec_t<3, wp::float32> var_13{};
    wp::quat_t<wp::float32> var_14{};
    wp::vec_t<3, wp::float32> var_15{};
    //---------
    // forward
    // def transform_2d_rotational_axes(                                                      <L 40>
    // q_off = wp.quat_from_matrix(wp.matrix_from_cols(axis_0, axis_1, wp.cross(axis_0, axis_1)))       <L 51>
    var_0 = wp::cross(var_axis_0, var_axis_1);
    var_1 = wp::matrix_from_cols<wp::float32>(var_axis_0, var_axis_1, var_0);
    var_2 = wp::quat_from_matrix(var_1);
    // local_0 = wp.quat_rotate(q_off, wp.vec3(1.0, 0.0, 0.0))                                <L 54>
    var_6 = wp::vec_t<3, wp::float32>(var_3, var_4, var_5);
    var_7 = wp::quat_rotate(var_2, var_6);
    // local_1 = wp.quat_rotate(q_off, wp.vec3(0.0, 1.0, 0.0))                                <L 55>
    var_11 = wp::vec_t<3, wp::float32>(var_8, var_9, var_10);
    var_12 = wp::quat_rotate(var_2, var_11);
    // a0 = local_0                                                                           <L 57>
    var_13 = wp::copy(var_7);
    // q_0 = wp.quat_from_axis_angle(a0, q0)                                                  <L 58>
    var_14 = wp::quat_from_axis_angle(var_13, var_q0);
    // a1 = wp.quat_rotate(q_0, local_1)                                                      <L 59>
    var_15 = wp::quat_rotate(var_14, var_12);
    // return a0, a1                                                                          <L 61>
    ret_0 = var_13;
    ret_1 = var_15;
    return;
}


// newton/_src/sim/articulation.py:64
static void compute_2d_rotational_dofs_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::float32 var_q0,
    wp::float32 var_q1,
    wp::float32 var_qd0,
    wp::float32 var_qd1,
    wp::quat_t<wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::quat_t<wp::float32> var_2{};
    wp::quat_t<wp::float32> var_3{};
    wp::quat_t<wp::float32> var_4{};
    wp::vec_t<3, wp::float32> var_5{};
    wp::vec_t<3, wp::float32> var_6{};
    wp::vec_t<3, wp::float32> var_7{};
    //---------
    // forward
    // def compute_2d_rotational_dofs(                                                        <L 65>
    // a0, a1 = transform_2d_rotational_axes(axis_0, axis_1, q0)                              <L 76>
    transform_2d_rotational_axes_0(var_axis_0, var_axis_1, var_q0, var_0, var_1);
    // q_0 = wp.quat_from_axis_angle(a0, q0)                                                  <L 78>
    var_2 = wp::quat_from_axis_angle(var_0, var_q0);
    // q_1 = wp.quat_from_axis_angle(a1, q1)                                                  <L 79>
    var_3 = wp::quat_from_axis_angle(var_1, var_q1);
    // rot = q_1 * q_0                                                                        <L 81>
    var_4 = wp::mul(var_3, var_2);
    // vel = a0 * qd0 + a1 * qd1                                                              <L 83>
    var_5 = wp::mul(var_0, var_qd0);
    var_6 = wp::mul(var_1, var_qd1);
    var_7 = wp::add(var_5, var_6);
    // return rot, vel                                                                        <L 85>
    ret_0 = var_4;
    ret_1 = var_7;
    return;
}


// newton/_src/sim/articulation.py:106
static void transform_3d_rotational_axes_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::vec_t<3, wp::float32> var_axis_2,
    wp::float32 var_q0,
    wp::float32 var_q1,
    wp::vec_t<3, wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1,
    wp::vec_t<3, wp::float32> & ret_2)
{
    //---------
    // primal vars
    wp::quat_t<wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::quat_t<wp::float32> var_2{};
    wp::quat_t<wp::float32> var_3{};
    wp::vec_t<3, wp::float32> var_4{};
    //---------
    // forward
    // def transform_3d_rotational_axes(                                                      <L 107>
    // q_0 = wp.quat_from_axis_angle(axis_0, q0)                                              <L 120>
    var_0 = wp::quat_from_axis_angle(var_axis_0, var_q0);
    // axis_1_w = wp.quat_rotate(q_0, axis_1)                                                 <L 122>
    var_1 = wp::quat_rotate(var_0, var_axis_1);
    // q_1 = wp.quat_from_axis_angle(axis_1_w, q1)                                            <L 123>
    var_2 = wp::quat_from_axis_angle(var_1, var_q1);
    // axis_2_w = wp.quat_rotate(q_1 * q_0, axis_2)                                           <L 125>
    var_3 = wp::mul(var_2, var_0);
    var_4 = wp::quat_rotate(var_3, var_axis_2);
    // return axis_0, axis_1_w, axis_2_w                                                      <L 127>
    ret_0 = var_axis_0;
    ret_1 = var_1;
    ret_2 = var_4;
    return;
}


// newton/_src/sim/articulation.py:130
static void compute_3d_rotational_dofs_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::vec_t<3, wp::float32> var_axis_2,
    wp::float32 var_q0,
    wp::float32 var_q1,
    wp::float32 var_q2,
    wp::float32 var_qd0,
    wp::float32 var_qd1,
    wp::float32 var_qd2,
    wp::quat_t<wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::quat_t<wp::float32> var_3{};
    wp::quat_t<wp::float32> var_4{};
    wp::quat_t<wp::float32> var_5{};
    wp::quat_t<wp::float32> var_6{};
    wp::quat_t<wp::float32> var_7{};
    wp::vec_t<3, wp::float32> var_8{};
    wp::vec_t<3, wp::float32> var_9{};
    wp::vec_t<3, wp::float32> var_10{};
    wp::vec_t<3, wp::float32> var_11{};
    wp::vec_t<3, wp::float32> var_12{};
    //---------
    // forward
    // def compute_3d_rotational_dofs(                                                        <L 131>
    // axis_0_w, axis_1_w, axis_2_w = transform_3d_rotational_axes(axis_0, axis_1, axis_2, q0, q1)       <L 148>
    transform_3d_rotational_axes_0(var_axis_0, var_axis_1, var_axis_2, var_q0, var_q1, var_0, var_1, var_2);
    // q_0 = wp.quat_from_axis_angle(axis_0_w, q0)                                            <L 150>
    var_3 = wp::quat_from_axis_angle(var_0, var_q0);
    // q_1 = wp.quat_from_axis_angle(axis_1_w, q1)                                            <L 151>
    var_4 = wp::quat_from_axis_angle(var_1, var_q1);
    // q_2 = wp.quat_from_axis_angle(axis_2_w, q2)                                            <L 152>
    var_5 = wp::quat_from_axis_angle(var_2, var_q2);
    // rot = q_2 * q_1 * q_0                                                                  <L 154>
    var_6 = wp::mul(var_5, var_4);
    var_7 = wp::mul(var_6, var_3);
    // vel = axis_0_w * qd0 + axis_1_w * qd1 + axis_2_w * qd2                                 <L 155>
    var_8 = wp::mul(var_0, var_qd0);
    var_9 = wp::mul(var_1, var_qd1);
    var_10 = wp::add(var_8, var_9);
    var_11 = wp::mul(var_2, var_qd2);
    var_12 = wp::add(var_10, var_11);
    // return rot, vel                                                                        <L 157>
    ret_0 = var_7;
    ret_1 = var_12;
    return;
}


// newton/_src/solvers/featherstone/kernels.py:262
static wp::transform_t<wp::float32> jcalc_transform_0(
    wp::int32 var_type,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::int32 var_axis_start,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::array_t<wp::float32> var_joint_q,
    wp::int32 var_q_start)
{
    //---------
    // primal vars
    const wp::int32 var_0 = 0;
    bool var_1{};
    wp::float32* var_2{};
    wp::float32 var_3{};
    wp::float32 var_4{};
    wp::vec_t<3, wp::float32>* var_5{};
    wp::vec_t<3, wp::float32> var_6{};
    wp::vec_t<3, wp::float32> var_7{};
    wp::vec_t<3, wp::float32> var_8{};
    wp::quat_t<wp::float32> var_9{};
    wp::transform_t<wp::float32> var_10{};
    const wp::int32 var_11 = 1;
    bool var_12{};
    wp::float32* var_13{};
    wp::float32 var_14{};
    wp::float32 var_15{};
    wp::vec_t<3, wp::float32>* var_16{};
    wp::vec_t<3, wp::float32> var_17{};
    wp::vec_t<3, wp::float32> var_18{};
    wp::vec_t<3, wp::float32> var_19{};
    wp::quat_t<wp::float32> var_20{};
    wp::transform_t<wp::float32> var_21{};
    wp::float32 var_22{};
    wp::vec_t<3, wp::float32> var_23{};
    wp::transform_t<wp::float32> var_24{};
    const wp::int32 var_25 = 2;
    bool var_26{};
    const wp::int32 var_27 = 0;
    wp::int32 var_28{};
    wp::float32* var_29{};
    wp::float32 var_30{};
    wp::float32 var_31{};
    const wp::int32 var_32 = 1;
    wp::int32 var_33{};
    wp::float32* var_34{};
    wp::float32 var_35{};
    wp::float32 var_36{};
    const wp::int32 var_37 = 2;
    wp::int32 var_38{};
    wp::float32* var_39{};
    wp::float32 var_40{};
    wp::float32 var_41{};
    const wp::int32 var_42 = 3;
    wp::int32 var_43{};
    wp::float32* var_44{};
    wp::float32 var_45{};
    wp::float32 var_46{};
    wp::vec_t<3, wp::float32> var_47{};
    wp::quat_t<wp::float32> var_48{};
    wp::transform_t<wp::float32> var_49{};
    wp::transform_t<wp::float32> var_50{};
    const wp::int32 var_51 = 3;
    bool var_52{};
    wp::transform_t<wp::float32> var_53{};
    wp::transform_t<wp::float32> var_54{};
    bool var_55{};
    const wp::int32 var_56 = 4;
    bool var_57{};
    const wp::int32 var_58 = 5;
    bool var_59{};
    const wp::int32 var_60 = 0;
    wp::int32 var_61{};
    wp::float32* var_62{};
    wp::float32 var_63{};
    wp::float32 var_64{};
    const wp::int32 var_65 = 1;
    wp::int32 var_66{};
    wp::float32* var_67{};
    wp::float32 var_68{};
    wp::float32 var_69{};
    const wp::int32 var_70 = 2;
    wp::int32 var_71{};
    wp::float32* var_72{};
    wp::float32 var_73{};
    wp::float32 var_74{};
    const wp::int32 var_75 = 3;
    wp::int32 var_76{};
    wp::float32* var_77{};
    wp::float32 var_78{};
    wp::float32 var_79{};
    const wp::int32 var_80 = 4;
    wp::int32 var_81{};
    wp::float32* var_82{};
    wp::float32 var_83{};
    wp::float32 var_84{};
    const wp::int32 var_85 = 5;
    wp::int32 var_86{};
    wp::float32* var_87{};
    wp::float32 var_88{};
    wp::float32 var_89{};
    const wp::int32 var_90 = 6;
    wp::int32 var_91{};
    wp::float32* var_92{};
    wp::float32 var_93{};
    wp::float32 var_94{};
    wp::vec_t<3, wp::float32> var_95{};
    wp::quat_t<wp::float32> var_96{};
    wp::transform_t<wp::float32> var_97{};
    wp::transform_t<wp::float32> var_98{};
    wp::float32 var_99{};
    wp::float32 var_100{};
    wp::float32 var_101{};
    wp::float32 var_102{};
    const wp::int32 var_103 = 6;
    bool var_104{};
    const wp::float32 var_105 = 0.0;
    wp::vec_t<3, wp::float32> var_106{};
    wp::quat_t<wp::float32> var_107{};
    const wp::int32 var_108 = 0;
    bool var_109{};
    const wp::int32 var_110 = 0;
    wp::int32 var_111{};
    wp::vec_t<3, wp::float32>* var_112{};
    wp::vec_t<3, wp::float32> var_113{};
    wp::vec_t<3, wp::float32> var_114{};
    const wp::int32 var_115 = 0;
    wp::int32 var_116{};
    wp::float32* var_117{};
    wp::vec_t<3, wp::float32> var_118{};
    wp::float32 var_119{};
    wp::vec_t<3, wp::float32> var_120{};
    wp::vec_t<3, wp::float32> var_121{};
    wp::vec_t<3, wp::float32> var_122{};
    const wp::int32 var_123 = 1;
    bool var_124{};
    const wp::int32 var_125 = 1;
    wp::int32 var_126{};
    wp::vec_t<3, wp::float32>* var_127{};
    wp::vec_t<3, wp::float32> var_128{};
    wp::vec_t<3, wp::float32> var_129{};
    const wp::int32 var_130 = 1;
    wp::int32 var_131{};
    wp::float32* var_132{};
    wp::vec_t<3, wp::float32> var_133{};
    wp::float32 var_134{};
    wp::vec_t<3, wp::float32> var_135{};
    wp::vec_t<3, wp::float32> var_136{};
    wp::vec_t<3, wp::float32> var_137{};
    const wp::int32 var_138 = 2;
    bool var_139{};
    const wp::int32 var_140 = 2;
    wp::int32 var_141{};
    wp::vec_t<3, wp::float32>* var_142{};
    wp::vec_t<3, wp::float32> var_143{};
    wp::vec_t<3, wp::float32> var_144{};
    const wp::int32 var_145 = 2;
    wp::int32 var_146{};
    wp::float32* var_147{};
    wp::vec_t<3, wp::float32> var_148{};
    wp::float32 var_149{};
    wp::vec_t<3, wp::float32> var_150{};
    wp::vec_t<3, wp::float32> var_151{};
    wp::vec_t<3, wp::float32> var_152{};
    wp::int32 var_153{};
    wp::int32 var_154{};
    const wp::int32 var_155 = 1;
    bool var_156{};
    wp::vec_t<3, wp::float32>* var_157{};
    wp::vec_t<3, wp::float32> var_158{};
    wp::vec_t<3, wp::float32> var_159{};
    wp::float32* var_160{};
    wp::quat_t<wp::float32> var_161{};
    wp::float32 var_162{};
    wp::vec_t<3, wp::float32> var_163{};
    wp::quat_t<wp::float32> var_164{};
    const wp::int32 var_165 = 2;
    bool var_166{};
    const wp::int32 var_167 = 0;
    wp::int32 var_168{};
    wp::vec_t<3, wp::float32>* var_169{};
    const wp::int32 var_170 = 1;
    wp::int32 var_171{};
    wp::vec_t<3, wp::float32>* var_172{};
    const wp::int32 var_173 = 0;
    wp::int32 var_174{};
    wp::float32* var_175{};
    const wp::int32 var_176 = 1;
    wp::int32 var_177{};
    wp::float32* var_178{};
    const wp::float32 var_179 = 0.0;
    const wp::float32 var_180 = 0.0;
    wp::quat_t<wp::float32> var_181{};
    wp::vec_t<3, wp::float32> var_182{};
    wp::vec_t<3, wp::float32> var_183{};
    wp::vec_t<3, wp::float32> var_184{};
    wp::float32 var_185{};
    wp::float32 var_186{};
    wp::quat_t<wp::float32> var_187{};
    const wp::int32 var_188 = 3;
    bool var_189{};
    const wp::int32 var_190 = 0;
    wp::int32 var_191{};
    wp::vec_t<3, wp::float32>* var_192{};
    const wp::int32 var_193 = 1;
    wp::int32 var_194{};
    wp::vec_t<3, wp::float32>* var_195{};
    const wp::int32 var_196 = 2;
    wp::int32 var_197{};
    wp::vec_t<3, wp::float32>* var_198{};
    const wp::int32 var_199 = 0;
    wp::int32 var_200{};
    wp::float32* var_201{};
    const wp::int32 var_202 = 1;
    wp::int32 var_203{};
    wp::float32* var_204{};
    const wp::int32 var_205 = 2;
    wp::int32 var_206{};
    wp::float32* var_207{};
    const wp::float32 var_208 = 0.0;
    const wp::float32 var_209 = 0.0;
    const wp::float32 var_210 = 0.0;
    wp::quat_t<wp::float32> var_211{};
    wp::vec_t<3, wp::float32> var_212{};
    wp::vec_t<3, wp::float32> var_213{};
    wp::vec_t<3, wp::float32> var_214{};
    wp::vec_t<3, wp::float32> var_215{};
    wp::float32 var_216{};
    wp::float32 var_217{};
    wp::float32 var_218{};
    wp::quat_t<wp::float32> var_219{};
    wp::vec_t<3, wp::float32> var_220{};
    wp::transform_t<wp::float32> var_221{};
    wp::vec_t<3, wp::float32> var_222{};
    wp::transform_t<wp::float32> var_223{};
    wp::transform_t<wp::float32> var_224{};
    //---------
    // forward
    // def jcalc_transform(                                                                   <L 263>
    // if type == JointType.PRISMATIC:                                                        <L 272>
    var_1 = (var_type == var_0);
    if (var_1) {
        // q = joint_q[q_start]                                                               <L 273>
        var_2 = wp::address(var_joint_q, var_q_start);
        var_4 = wp::load(var_2);
        var_3 = wp::copy(var_4);
        // axis = joint_axis[axis_start]                                                      <L 274>
        var_5 = wp::address(var_joint_axis, var_axis_start);
        var_7 = wp::load(var_5);
        var_6 = wp::copy(var_7);
        // X_jc = wp.transform(axis * q, wp.quat_identity())                                  <L 275>
        var_8 = wp::mul(var_6, var_3);
        var_9 = wp::quat_identity<wp::float32>();
        var_10 = wp::transform_t<wp::float32>(var_8, var_9);
        // return X_jc                                                                        <L 276>
        return var_10;
    }
    // if type == JointType.REVOLUTE:                                                         <L 278>
    var_12 = (var_type == var_11);
    if (var_12) {
        // q = joint_q[q_start]                                                               <L 279>
        var_13 = wp::address(var_joint_q, var_q_start);
        var_15 = wp::load(var_13);
        var_14 = wp::copy(var_15);
        // axis = joint_axis[axis_start]                                                      <L 280>
        var_16 = wp::address(var_joint_axis, var_axis_start);
        var_18 = wp::load(var_16);
        var_17 = wp::copy(var_18);
        // X_jc = wp.transform(wp.vec3(), wp.quat_from_axis_angle(axis, q))                   <L 281>
        var_19 = wp::vec_t<3, wp::float32>();
        var_20 = wp::quat_from_axis_angle(var_17, var_14);
        var_21 = wp::transform_t<wp::float32>(var_19, var_20);
        // return X_jc                                                                        <L 282>
        return var_21;
    }
    var_22 = wp::where(var_12, var_14, var_3);
    var_23 = wp::where(var_12, var_17, var_6);
    var_24 = wp::where(var_12, var_21, var_10);
    // if type == JointType.BALL:                                                             <L 284>
    var_26 = (var_type == var_25);
    if (var_26) {
        // qx = joint_q[q_start + 0]                                                          <L 285>
        var_28 = wp::add(var_q_start, var_27);
        var_29 = wp::address(var_joint_q, var_28);
        var_31 = wp::load(var_29);
        var_30 = wp::copy(var_31);
        // qy = joint_q[q_start + 1]                                                          <L 286>
        var_33 = wp::add(var_q_start, var_32);
        var_34 = wp::address(var_joint_q, var_33);
        var_36 = wp::load(var_34);
        var_35 = wp::copy(var_36);
        // qz = joint_q[q_start + 2]                                                          <L 287>
        var_38 = wp::add(var_q_start, var_37);
        var_39 = wp::address(var_joint_q, var_38);
        var_41 = wp::load(var_39);
        var_40 = wp::copy(var_41);
        // qw = joint_q[q_start + 3]                                                          <L 288>
        var_43 = wp::add(var_q_start, var_42);
        var_44 = wp::address(var_joint_q, var_43);
        var_46 = wp::load(var_44);
        var_45 = wp::copy(var_46);
        // X_jc = wp.transform(wp.vec3(), wp.quat(qx, qy, qz, qw))                            <L 290>
        var_47 = wp::vec_t<3, wp::float32>();
        var_48 = wp::quat_t<wp::float32>(var_30, var_35, var_40, var_45);
        var_49 = wp::transform_t<wp::float32>(var_47, var_48);
        // return X_jc                                                                        <L 291>
        return var_49;
    }
    var_50 = wp::where(var_26, var_49, var_24);
    // if type == JointType.FIXED:                                                            <L 293>
    var_52 = (var_type == var_51);
    if (var_52) {
        // X_jc = wp.transform_identity()                                                     <L 294>
        var_53 = wp::transform_identity<wp::float32>();
        // return X_jc                                                                        <L 295>
        return var_53;
    }
    var_54 = wp::where(var_52, var_53, var_50);
    // if type == JointType.FREE or type == JointType.DISTANCE:                               <L 297>
    var_57 = (var_type == var_56);
    var_55 = var_57;
    if (!var_55) {
        var_59 = (var_type == var_58);
        var_55 = var_55 || var_59;
    }
    if (var_55) {
        // px = joint_q[q_start + 0]                                                          <L 298>
        var_61 = wp::add(var_q_start, var_60);
        var_62 = wp::address(var_joint_q, var_61);
        var_64 = wp::load(var_62);
        var_63 = wp::copy(var_64);
        // py = joint_q[q_start + 1]                                                          <L 299>
        var_66 = wp::add(var_q_start, var_65);
        var_67 = wp::address(var_joint_q, var_66);
        var_69 = wp::load(var_67);
        var_68 = wp::copy(var_69);
        // pz = joint_q[q_start + 2]                                                          <L 300>
        var_71 = wp::add(var_q_start, var_70);
        var_72 = wp::address(var_joint_q, var_71);
        var_74 = wp::load(var_72);
        var_73 = wp::copy(var_74);
        // qx = joint_q[q_start + 3]                                                          <L 302>
        var_76 = wp::add(var_q_start, var_75);
        var_77 = wp::address(var_joint_q, var_76);
        var_79 = wp::load(var_77);
        var_78 = wp::copy(var_79);
        // qy = joint_q[q_start + 4]                                                          <L 303>
        var_81 = wp::add(var_q_start, var_80);
        var_82 = wp::address(var_joint_q, var_81);
        var_84 = wp::load(var_82);
        var_83 = wp::copy(var_84);
        // qz = joint_q[q_start + 5]                                                          <L 304>
        var_86 = wp::add(var_q_start, var_85);
        var_87 = wp::address(var_joint_q, var_86);
        var_89 = wp::load(var_87);
        var_88 = wp::copy(var_89);
        // qw = joint_q[q_start + 6]                                                          <L 305>
        var_91 = wp::add(var_q_start, var_90);
        var_92 = wp::address(var_joint_q, var_91);
        var_94 = wp::load(var_92);
        var_93 = wp::copy(var_94);
        // X_jc = wp.transform(wp.vec3(px, py, pz), wp.quat(qx, qy, qz, qw))                  <L 307>
        var_95 = wp::vec_t<3, wp::float32>(var_63, var_68, var_73);
        var_96 = wp::quat_t<wp::float32>(var_78, var_83, var_88, var_93);
        var_97 = wp::transform_t<wp::float32>(var_95, var_96);
        // return X_jc                                                                        <L 308>
        return var_97;
    }
    var_98 = wp::where(var_55, var_97, var_54);
    var_99 = wp::where(var_55, var_78, var_30);
    var_100 = wp::where(var_55, var_83, var_35);
    var_101 = wp::where(var_55, var_88, var_40);
    var_102 = wp::where(var_55, var_93, var_45);
    // if type == JointType.D6:                                                               <L 310>
    var_104 = (var_type == var_103);
    if (var_104) {
        // pos = wp.vec3(0.0)                                                                 <L 311>
        var_106 = wp::vec_t<3, wp::float32>(var_105);
        // rot = wp.quat_identity()                                                           <L 312>
        var_107 = wp::quat_identity<wp::float32>();
        // if lin_axis_count > 0:                                                             <L 317>
        var_109 = (var_lin_axis_count > var_108);
        if (var_109) {
            // axis = joint_axis[axis_start + 0]                                              <L 318>
            var_111 = wp::add(var_axis_start, var_110);
            var_112 = wp::address(var_joint_axis, var_111);
            var_114 = wp::load(var_112);
            var_113 = wp::copy(var_114);
            // pos += axis * joint_q[q_start + 0]                                             <L 319>
            var_116 = wp::add(var_q_start, var_115);
            var_117 = wp::address(var_joint_q, var_116);
            var_119 = wp::load(var_117);
            var_118 = wp::mul(var_113, var_119);
            var_120 = wp::add(var_106, var_118);
        }
        var_121 = wp::where(var_109, var_113, var_23);
        var_122 = wp::where(var_109, var_120, var_106);
        // if lin_axis_count > 1:                                                             <L 320>
        var_124 = (var_lin_axis_count > var_123);
        if (var_124) {
            // axis = joint_axis[axis_start + 1]                                              <L 321>
            var_126 = wp::add(var_axis_start, var_125);
            var_127 = wp::address(var_joint_axis, var_126);
            var_129 = wp::load(var_127);
            var_128 = wp::copy(var_129);
            // pos += axis * joint_q[q_start + 1]                                             <L 322>
            var_131 = wp::add(var_q_start, var_130);
            var_132 = wp::address(var_joint_q, var_131);
            var_134 = wp::load(var_132);
            var_133 = wp::mul(var_128, var_134);
            var_135 = wp::add(var_122, var_133);
        }
        var_136 = wp::where(var_124, var_128, var_121);
        var_137 = wp::where(var_124, var_135, var_122);
        // if lin_axis_count > 2:                                                             <L 323>
        var_139 = (var_lin_axis_count > var_138);
        if (var_139) {
            // axis = joint_axis[axis_start + 2]                                              <L 324>
            var_141 = wp::add(var_axis_start, var_140);
            var_142 = wp::address(var_joint_axis, var_141);
            var_144 = wp::load(var_142);
            var_143 = wp::copy(var_144);
            // pos += axis * joint_q[q_start + 2]                                             <L 325>
            var_146 = wp::add(var_q_start, var_145);
            var_147 = wp::address(var_joint_q, var_146);
            var_149 = wp::load(var_147);
            var_148 = wp::mul(var_143, var_149);
            var_150 = wp::add(var_137, var_148);
        }
        var_151 = wp::where(var_139, var_143, var_136);
        var_152 = wp::where(var_139, var_150, var_137);
        // ia = axis_start + lin_axis_count                                                   <L 327>
        var_153 = wp::add(var_axis_start, var_lin_axis_count);
        // iq = q_start + lin_axis_count                                                      <L 328>
        var_154 = wp::add(var_q_start, var_lin_axis_count);
        // if ang_axis_count == 1:                                                            <L 329>
        var_156 = (var_ang_axis_count == var_155);
        if (var_156) {
            // axis = joint_axis[ia]                                                          <L 330>
            var_157 = wp::address(var_joint_axis, var_153);
            var_159 = wp::load(var_157);
            var_158 = wp::copy(var_159);
            // rot = wp.quat_from_axis_angle(axis, joint_q[iq])                               <L 331>
            var_160 = wp::address(var_joint_q, var_154);
            var_162 = wp::load(var_160);
            var_161 = wp::quat_from_axis_angle(var_158, var_162);
        }
        var_163 = wp::where(var_156, var_158, var_151);
        var_164 = wp::where(var_156, var_161, var_107);
        // if ang_axis_count == 2:                                                            <L 332>
        var_166 = (var_ang_axis_count == var_165);
        if (var_166) {
            // rot, _ = compute_2d_rotational_dofs(                                           <L 333>
            // joint_axis[ia + 0],                                                            <L 334>
            var_168 = wp::add(var_153, var_167);
            var_169 = wp::address(var_joint_axis, var_168);
            // joint_axis[ia + 1],                                                            <L 335>
            var_171 = wp::add(var_153, var_170);
            var_172 = wp::address(var_joint_axis, var_171);
            // joint_q[iq + 0],                                                               <L 336>
            var_174 = wp::add(var_154, var_173);
            var_175 = wp::address(var_joint_q, var_174);
            // joint_q[iq + 1],                                                               <L 337>
            var_177 = wp::add(var_154, var_176);
            var_178 = wp::address(var_joint_q, var_177);
            // 0.0,                                                                           <L 338>
            // 0.0,                                                                           <L 339>
            var_183 = wp::load(var_169);
            var_184 = wp::load(var_172);
            var_185 = wp::load(var_175);
            var_186 = wp::load(var_178);
            compute_2d_rotational_dofs_0(var_183, var_184, var_185, var_186, var_179, var_180, var_181, var_182);
        }
        var_187 = wp::where(var_166, var_181, var_164);
        // if ang_axis_count == 3:                                                            <L 341>
        var_189 = (var_ang_axis_count == var_188);
        if (var_189) {
            // rot, _ = compute_3d_rotational_dofs(                                           <L 342>
            // joint_axis[ia + 0],                                                            <L 343>
            var_191 = wp::add(var_153, var_190);
            var_192 = wp::address(var_joint_axis, var_191);
            // joint_axis[ia + 1],                                                            <L 344>
            var_194 = wp::add(var_153, var_193);
            var_195 = wp::address(var_joint_axis, var_194);
            // joint_axis[ia + 2],                                                            <L 345>
            var_197 = wp::add(var_153, var_196);
            var_198 = wp::address(var_joint_axis, var_197);
            // joint_q[iq + 0],                                                               <L 346>
            var_200 = wp::add(var_154, var_199);
            var_201 = wp::address(var_joint_q, var_200);
            // joint_q[iq + 1],                                                               <L 347>
            var_203 = wp::add(var_154, var_202);
            var_204 = wp::address(var_joint_q, var_203);
            // joint_q[iq + 2],                                                               <L 348>
            var_206 = wp::add(var_154, var_205);
            var_207 = wp::address(var_joint_q, var_206);
            // 0.0,                                                                           <L 349>
            // 0.0,                                                                           <L 350>
            // 0.0,                                                                           <L 351>
            var_213 = wp::load(var_192);
            var_214 = wp::load(var_195);
            var_215 = wp::load(var_198);
            var_216 = wp::load(var_201);
            var_217 = wp::load(var_204);
            var_218 = wp::load(var_207);
            compute_3d_rotational_dofs_0(var_213, var_214, var_215, var_216, var_217, var_218, var_208, var_209, var_210, var_211, var_212);
        }
        var_219 = wp::where(var_189, var_211, var_187);
        var_220 = wp::where(var_189, var_212, var_182);
        // X_jc = wp.transform(pos, rot)                                                      <L 354>
        var_221 = wp::transform_t<wp::float32>(var_152, var_219);
        // return X_jc                                                                        <L 355>
        return var_221;
    }
    var_222 = wp::where(var_104, var_163, var_23);
    var_223 = wp::where(var_104, var_221, var_98);
    // return wp.transform_identity()                                                         <L 358>
    var_224 = wp::transform_identity<wp::float32>();
    return var_224;
}


// newton/_src/solvers/featherstone/kernels.py:753
static void compute_link_transform_0(
    wp::int32 var_i,
    wp::array_t<wp::int32> var_joint_type,
    wp::array_t<wp::int32> var_joint_parent,
    wp::array_t<wp::int32> var_joint_child,
    wp::array_t<wp::int32> var_joint_q_start,
    wp::array_t<wp::int32> var_joint_qd_start,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c,
    wp::array_t<wp::transform_t<wp::float32>> var_body_X_com,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::int32> var_joint_dof_dim,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com)
{
    //---------
    // primal vars
    wp::int32* var_0{};
    wp::int32 var_1{};
    wp::int32 var_2{};
    wp::int32* var_3{};
    wp::int32 var_4{};
    wp::int32 var_5{};
    wp::transform_t<wp::float32>* var_6{};
    wp::transform_t<wp::float32> var_7{};
    wp::transform_t<wp::float32> var_8{};
    wp::transform_t<wp::float32>* var_9{};
    wp::transform_t<wp::float32> var_10{};
    wp::transform_t<wp::float32> var_11{};
    wp::transform_t<wp::float32> var_12{};
    const wp::int32 var_13 = 0;
    bool var_14{};
    wp::transform_t<wp::float32>* var_15{};
    wp::transform_t<wp::float32> var_16{};
    wp::transform_t<wp::float32> var_17{};
    wp::transform_t<wp::float32> var_18{};
    wp::transform_t<wp::float32> var_19{};
    wp::int32* var_20{};
    wp::int32 var_21{};
    wp::int32 var_22{};
    wp::int32* var_23{};
    wp::int32 var_24{};
    wp::int32 var_25{};
    const wp::int32 var_26 = 0;
    wp::int32* var_27{};
    wp::int32 var_28{};
    wp::int32 var_29{};
    const wp::int32 var_30 = 1;
    wp::int32* var_31{};
    wp::int32 var_32{};
    wp::int32 var_33{};
    wp::int32* var_34{};
    wp::int32 var_35{};
    wp::int32 var_36{};
    wp::transform_t<wp::float32> var_37{};
    wp::transform_t<wp::float32> var_38{};
    wp::transform_t<wp::float32> var_39{};
    wp::transform_t<wp::float32> var_40{};
    wp::transform_t<wp::float32>* var_41{};
    wp::transform_t<wp::float32> var_42{};
    wp::transform_t<wp::float32> var_43{};
    wp::transform_t<wp::float32> var_44{};
    //---------
    // forward
    // def compute_link_transform(                                                            <L 754>
    // parent = joint_parent[i]                                                               <L 772>
    var_0 = wp::address(var_joint_parent, var_i);
    var_2 = wp::load(var_0);
    var_1 = wp::copy(var_2);
    // child = joint_child[i]                                                                 <L 773>
    var_3 = wp::address(var_joint_child, var_i);
    var_5 = wp::load(var_3);
    var_4 = wp::copy(var_5);
    // X_pj = joint_X_p[i]                                                                    <L 776>
    var_6 = wp::address(var_joint_X_p, var_i);
    var_8 = wp::load(var_6);
    var_7 = wp::copy(var_8);
    // X_cj = joint_X_c[i]                                                                    <L 777>
    var_9 = wp::address(var_joint_X_c, var_i);
    var_11 = wp::load(var_9);
    var_10 = wp::copy(var_11);
    // X_wpj = X_pj                                                                           <L 779>
    var_12 = wp::copy(var_7);
    // if parent >= 0:                                                                        <L 780>
    var_14 = (var_1 >= var_13);
    if (var_14) {
        // X_wp = body_q[parent]                                                              <L 781>
        var_15 = wp::address(var_body_q, var_1);
        var_17 = wp::load(var_15);
        var_16 = wp::copy(var_17);
        // X_wpj = X_wp * X_wpj                                                               <L 782>
        var_18 = wp::mul(var_16, var_12);
    }
    var_19 = wp::where(var_14, var_18, var_12);
    // type = joint_type[i]                                                                   <L 784>
    var_20 = wp::address(var_joint_type, var_i);
    var_22 = wp::load(var_20);
    var_21 = wp::copy(var_22);
    // qd_start = joint_qd_start[i]                                                           <L 785>
    var_23 = wp::address(var_joint_qd_start, var_i);
    var_25 = wp::load(var_23);
    var_24 = wp::copy(var_25);
    // lin_axis_count = joint_dof_dim[i, 0]                                                   <L 786>
    var_27 = wp::address(var_joint_dof_dim, var_i, var_26);
    var_29 = wp::load(var_27);
    var_28 = wp::copy(var_29);
    // ang_axis_count = joint_dof_dim[i, 1]                                                   <L 787>
    var_31 = wp::address(var_joint_dof_dim, var_i, var_30);
    var_33 = wp::load(var_31);
    var_32 = wp::copy(var_33);
    // coord_start = joint_q_start[i]                                                         <L 788>
    var_34 = wp::address(var_joint_q_start, var_i);
    var_36 = wp::load(var_34);
    var_35 = wp::copy(var_36);
    // X_j = jcalc_transform(type, joint_axis, qd_start, lin_axis_count, ang_axis_count, joint_q, coord_start)       <L 791>
    var_37 = jcalc_transform_0(var_21, var_joint_axis, var_24, var_28, var_32, var_joint_q, var_35);
    // X_wcj = X_wpj * X_j                                                                    <L 794>
    var_38 = wp::mul(var_19, var_37);
    // X_wc = X_wcj * wp.transform_inverse(X_cj)                                              <L 796>
    var_39 = wp::transform_inverse(var_10);
    var_40 = wp::mul(var_38, var_39);
    // X_cm = body_X_com[child]                                                               <L 799>
    var_41 = wp::address(var_body_X_com, var_4);
    var_43 = wp::load(var_41);
    var_42 = wp::copy(var_43);
    // X_sm = X_wc * X_cm                                                                     <L 800>
    var_44 = wp::mul(var_40, var_42);
    // body_q[child] = X_wc                                                                   <L 803>
    wp::array_store(var_body_q, var_4, var_40);
    // body_q_com[child] = X_sm                                                               <L 804>
    wp::array_store(var_body_q_com, var_4, var_44);
}


// warp/_src/math.py:131
static wp::vec_t<3, wp::float32> velocity_at_point_0(
    wp::vec_t<6, wp::float32> var_qd,
    wp::vec_t<3, wp::float32> var_r)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    //---------
    // forward
    // def velocity_at_point(qd: wp.spatial_vector, r: wp.vec3) -> wp.vec3:                   <L 132>
    // return wp.spatial_bottom(qd) + wp.cross(wp.spatial_top(qd), r)                         <L 148>
    var_0 = wp::spatial_bottom(var_qd);
    var_1 = wp::spatial_top(var_qd);
    var_2 = wp::cross(var_1, var_r);
    var_3 = wp::add(var_0, var_2);
    return var_3;
}


// newton/_src/math/spatial.py:53
static wp::vec_t<3, wp::float32> velocity_at_point_1(
    wp::vec_t<6, wp::float32> var_qd,
    wp::vec_t<3, wp::float32> var_r)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<6, wp::float32> var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    //---------
    // forward
    // def velocity_at_point(qd: wp.spatial_vector, r: wp.vec3) -> wp.vec3:                   <L 54>
    // qd_wp = wp.spatial_vector(wp.spatial_bottom(qd), wp.spatial_top(qd))                   <L 77>
    var_0 = wp::spatial_bottom(var_qd);
    var_1 = wp::spatial_top(var_qd);
    var_2 = wp::vec_t<6, wp::float32>(var_0, var_1);
    // return wp.velocity_at_point(qd_wp, r)                                                  <L 78>
    var_3 = velocity_at_point_0(var_2, var_r);
    return var_3;
}


// newton/_src/sim/articulation.py:17
static wp::vec_t<3, wp::float32> com_twist_to_point_velocity_0(
    wp::vec_t<6, wp::float32> var_qd,
    wp::transform_t<wp::float32> var_X_wb,
    wp::vec_t<3, wp::float32> var_body_com,
    wp::vec_t<3, wp::float32> var_point)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    //---------
    // forward
    // def com_twist_to_point_velocity(qd: wp.spatial_vector, X_wb: wp.transform, body_com: wp.vec3, point: wp.vec3):       <L 18>
    // return velocity_at_point(qd, point - wp.transform_point(X_wb, body_com))               <L 20>
    var_0 = wp::transform_point(var_X_wb, var_body_com);
    var_1 = wp::sub(var_point, var_0);
    var_2 = velocity_at_point_1(var_qd, var_1);
    return var_2;
}


// warp/_src/math.py:452
static wp::vec_t<6, wp::float32> transform_twist_0(
    wp::transform_t<wp::float32> var_t,
    wp::vec_t<6, wp::float32> var_x)
{
    //---------
    // primal vars
    wp::quat_t<wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    wp::vec_t<3, wp::float32> var_4{};
    wp::vec_t<3, wp::float32> var_5{};
    wp::vec_t<3, wp::float32> var_6{};
    wp::vec_t<3, wp::float32> var_7{};
    wp::vec_t<6, wp::float32> var_8{};
    //---------
    // forward
    // def transform_twist(t: wp.transform, x: wp.spatial_vector) -> wp.spatial_vector:       <L 453>
    // q = wp.transform_get_rotation(t)                                                       <L 469>
    var_0 = wp::transform_get_rotation(var_t);
    // p = wp.transform_get_translation(t)                                                    <L 470>
    var_1 = wp::transform_get_translation(var_t);
    // w = wp.spatial_top(x)                                                                  <L 472>
    var_2 = wp::spatial_top(var_x);
    // v = wp.spatial_bottom(x)                                                               <L 473>
    var_3 = wp::spatial_bottom(var_x);
    // w = wp.quat_rotate(q, w)                                                               <L 475>
    var_4 = wp::quat_rotate(var_0, var_2);
    // v = wp.quat_rotate(q, v) + wp.cross(p, w)                                              <L 476>
    var_5 = wp::quat_rotate(var_0, var_3);
    var_6 = wp::cross(var_1, var_4);
    var_7 = wp::add(var_5, var_6);
    // return wp.spatial_vector(w, v)                                                         <L 478>
    var_8 = wp::vec_t<6, wp::float32>(var_4, var_7);
    return var_8;
}


// newton/_src/math/spatial.py:81
static wp::vec_t<6, wp::float32> transform_twist_1(
    wp::transform_t<wp::float32> var_t,
    wp::vec_t<6, wp::float32> var_x)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<6, wp::float32> var_2{};
    wp::vec_t<6, wp::float32> var_3{};
    wp::vec_t<3, wp::float32> var_4{};
    wp::vec_t<3, wp::float32> var_5{};
    wp::vec_t<6, wp::float32> var_6{};
    //---------
    // forward
    // def transform_twist(t: wp.transform, x: wp.spatial_vector) -> wp.spatial_vector:       <L 82>
    // x_wp = wp.spatial_vector(wp.spatial_bottom(x), wp.spatial_top(x))                      <L 102>
    var_0 = wp::spatial_bottom(var_x);
    var_1 = wp::spatial_top(var_x);
    var_2 = wp::vec_t<6, wp::float32>(var_0, var_1);
    // y_wp = wp.transform_twist(t, x_wp)                                                     <L 103>
    var_3 = transform_twist_0(var_t, var_2);
    // return wp.spatial_vector(wp.spatial_bottom(y_wp), wp.spatial_top(y_wp))                <L 104>
    var_4 = wp::spatial_bottom(var_3);
    var_5 = wp::spatial_top(var_3);
    var_6 = wp::vec_t<6, wp::float32>(var_4, var_5);
    return var_6;
}


// newton/_src/sim/articulation.py:23
static wp::vec_t<6, wp::float32> origin_twist_to_com_twist_0(
    wp::vec_t<6, wp::float32> var_qd,
    wp::transform_t<wp::float32> var_X_wb,
    wp::vec_t<3, wp::float32> var_body_com)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<6, wp::float32> var_3{};
    //---------
    // forward
    // def origin_twist_to_com_twist(qd: wp.spatial_vector, X_wb: wp.transform, body_com: wp.vec3):       <L 24>
    // omega = wp.spatial_bottom(qd)                                                          <L 26>
    var_0 = wp::spatial_bottom(var_qd);
    // v_com = velocity_at_point(qd, wp.transform_vector(X_wb, body_com))                     <L 27>
    var_1 = wp::transform_vector(var_X_wb, var_body_com);
    var_2 = velocity_at_point_1(var_qd, var_1);
    // return wp.spatial_vector(v_com, omega)                                                 <L 28>
    var_3 = wp::vec_t<6, wp::float32>(var_2, var_0);
    return var_3;
}


// newton/_src/solvers/featherstone/kernels.py:1996
static void eval_single_articulation_fk_with_velocity_conversion_0(
    wp::int32 var_joint_start,
    wp::int32 var_joint_end,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::int32> var_joint_q_start,
    wp::array_t<wp::int32> var_joint_qd_start,
    wp::array_t<wp::int32> var_joint_type,
    wp::array_t<wp::int32> var_joint_parent,
    wp::array_t<wp::int32> var_joint_child,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::int32> var_joint_dof_dim,
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd)
{
    //---------
    // primal vars
    wp::range_t var_0{};
    wp::int32 var_1{};
    wp::int32* var_2{};
    wp::int32 var_3{};
    wp::int32 var_4{};
    wp::int32* var_5{};
    wp::int32 var_6{};
    wp::int32 var_7{};
    wp::int32* var_8{};
    wp::int32 var_9{};
    wp::int32 var_10{};
    wp::transform_t<wp::float32>* var_11{};
    wp::transform_t<wp::float32> var_12{};
    wp::transform_t<wp::float32> var_13{};
    wp::transform_t<wp::float32>* var_14{};
    wp::transform_t<wp::float32> var_15{};
    wp::transform_t<wp::float32> var_16{};
    wp::int32* var_17{};
    wp::int32 var_18{};
    wp::int32 var_19{};
    wp::int32* var_20{};
    wp::int32 var_21{};
    wp::int32 var_22{};
    const wp::int32 var_23 = 0;
    wp::int32* var_24{};
    wp::int32 var_25{};
    wp::int32 var_26{};
    const wp::int32 var_27 = 1;
    wp::int32* var_28{};
    wp::int32 var_29{};
    wp::int32 var_30{};
    wp::transform_t<wp::float32> var_31{};
    wp::vec_t<3, wp::float32> var_32{};
    wp::vec_t<3, wp::float32> var_33{};
    wp::vec_t<6, wp::float32> var_34{};
    const wp::int32 var_35 = 0;
    bool var_36{};
    wp::vec_t<3, wp::float32>* var_37{};
    wp::vec_t<3, wp::float32> var_38{};
    wp::vec_t<3, wp::float32> var_39{};
    wp::float32* var_40{};
    wp::float32 var_41{};
    wp::float32 var_42{};
    wp::float32* var_43{};
    wp::float32 var_44{};
    wp::float32 var_45{};
    wp::vec_t<3, wp::float32> var_46{};
    wp::quat_t<wp::float32> var_47{};
    wp::transform_t<wp::float32> var_48{};
    wp::vec_t<3, wp::float32> var_49{};
    wp::vec_t<3, wp::float32> var_50{};
    wp::vec_t<6, wp::float32> var_51{};
    wp::transform_t<wp::float32> var_52{};
    wp::vec_t<6, wp::float32> var_53{};
    const wp::int32 var_54 = 1;
    bool var_55{};
    wp::vec_t<3, wp::float32>* var_56{};
    wp::vec_t<3, wp::float32> var_57{};
    wp::vec_t<3, wp::float32> var_58{};
    wp::float32* var_59{};
    wp::float32 var_60{};
    wp::float32 var_61{};
    wp::float32* var_62{};
    wp::float32 var_63{};
    wp::float32 var_64{};
    wp::vec_t<3, wp::float32> var_65{};
    wp::quat_t<wp::float32> var_66{};
    wp::transform_t<wp::float32> var_67{};
    wp::vec_t<3, wp::float32> var_68{};
    wp::vec_t<3, wp::float32> var_69{};
    wp::vec_t<6, wp::float32> var_70{};
    wp::transform_t<wp::float32> var_71{};
    wp::vec_t<6, wp::float32> var_72{};
    wp::vec_t<3, wp::float32> var_73{};
    wp::float32 var_74{};
    wp::float32 var_75{};
    const wp::int32 var_76 = 2;
    bool var_77{};
    const wp::int32 var_78 = 0;
    wp::int32 var_79{};
    wp::float32* var_80{};
    const wp::int32 var_81 = 1;
    wp::int32 var_82{};
    wp::float32* var_83{};
    const wp::int32 var_84 = 2;
    wp::int32 var_85{};
    wp::float32* var_86{};
    const wp::int32 var_87 = 3;
    wp::int32 var_88{};
    wp::float32* var_89{};
    wp::quat_t<wp::float32> var_90{};
    wp::float32 var_91{};
    wp::float32 var_92{};
    wp::float32 var_93{};
    wp::float32 var_94{};
    const wp::int32 var_95 = 0;
    wp::int32 var_96{};
    wp::float32* var_97{};
    const wp::int32 var_98 = 1;
    wp::int32 var_99{};
    wp::float32* var_100{};
    const wp::int32 var_101 = 2;
    wp::int32 var_102{};
    wp::float32* var_103{};
    wp::vec_t<3, wp::float32> var_104{};
    wp::float32 var_105{};
    wp::float32 var_106{};
    wp::float32 var_107{};
    wp::vec_t<3, wp::float32> var_108{};
    wp::transform_t<wp::float32> var_109{};
    wp::vec_t<3, wp::float32> var_110{};
    wp::vec_t<6, wp::float32> var_111{};
    wp::transform_t<wp::float32> var_112{};
    wp::vec_t<6, wp::float32> var_113{};
    bool var_114{};
    const wp::int32 var_115 = 4;
    bool var_116{};
    const wp::int32 var_117 = 5;
    bool var_118{};
    const wp::int32 var_119 = 0;
    wp::int32 var_120{};
    wp::float32* var_121{};
    const wp::int32 var_122 = 1;
    wp::int32 var_123{};
    wp::float32* var_124{};
    const wp::int32 var_125 = 2;
    wp::int32 var_126{};
    wp::float32* var_127{};
    wp::vec_t<3, wp::float32> var_128{};
    wp::float32 var_129{};
    wp::float32 var_130{};
    wp::float32 var_131{};
    const wp::int32 var_132 = 3;
    wp::int32 var_133{};
    wp::float32* var_134{};
    const wp::int32 var_135 = 4;
    wp::int32 var_136{};
    wp::float32* var_137{};
    const wp::int32 var_138 = 5;
    wp::int32 var_139{};
    wp::float32* var_140{};
    const wp::int32 var_141 = 6;
    wp::int32 var_142{};
    wp::float32* var_143{};
    wp::quat_t<wp::float32> var_144{};
    wp::float32 var_145{};
    wp::float32 var_146{};
    wp::float32 var_147{};
    wp::float32 var_148{};
    wp::transform_t<wp::float32> var_149{};
    const wp::int32 var_150 = 0;
    wp::int32 var_151{};
    wp::float32* var_152{};
    const wp::int32 var_153 = 1;
    wp::int32 var_154{};
    wp::float32* var_155{};
    const wp::int32 var_156 = 2;
    wp::int32 var_157{};
    wp::float32* var_158{};
    wp::vec_t<3, wp::float32> var_159{};
    wp::float32 var_160{};
    wp::float32 var_161{};
    wp::float32 var_162{};
    const wp::int32 var_163 = 3;
    wp::int32 var_164{};
    wp::float32* var_165{};
    const wp::int32 var_166 = 4;
    wp::int32 var_167{};
    wp::float32* var_168{};
    const wp::int32 var_169 = 5;
    wp::int32 var_170{};
    wp::float32* var_171{};
    wp::vec_t<3, wp::float32> var_172{};
    wp::float32 var_173{};
    wp::float32 var_174{};
    wp::float32 var_175{};
    wp::vec_t<6, wp::float32> var_176{};
    wp::transform_t<wp::float32> var_177{};
    wp::vec_t<6, wp::float32> var_178{};
    wp::transform_t<wp::float32> var_179{};
    wp::vec_t<6, wp::float32> var_180{};
    const wp::int32 var_181 = 6;
    bool var_182{};
    const wp::float32 var_183 = 0.0;
    wp::vec_t<3, wp::float32> var_184{};
    wp::quat_t<wp::float32> var_185{};
    const wp::float32 var_186 = 0.0;
    wp::vec_t<3, wp::float32> var_187{};
    const wp::float32 var_188 = 0.0;
    wp::vec_t<3, wp::float32> var_189{};
    const wp::int32 var_190 = 0;
    bool var_191{};
    const wp::int32 var_192 = 0;
    wp::int32 var_193{};
    wp::vec_t<3, wp::float32>* var_194{};
    wp::vec_t<3, wp::float32> var_195{};
    wp::vec_t<3, wp::float32> var_196{};
    const wp::int32 var_197 = 0;
    wp::int32 var_198{};
    wp::float32* var_199{};
    wp::vec_t<3, wp::float32> var_200{};
    wp::float32 var_201{};
    wp::vec_t<3, wp::float32> var_202{};
    const wp::int32 var_203 = 0;
    wp::int32 var_204{};
    wp::float32* var_205{};
    wp::vec_t<3, wp::float32> var_206{};
    wp::float32 var_207{};
    wp::vec_t<3, wp::float32> var_208{};
    wp::vec_t<3, wp::float32> var_209{};
    wp::vec_t<3, wp::float32> var_210{};
    wp::vec_t<3, wp::float32> var_211{};
    const wp::int32 var_212 = 1;
    bool var_213{};
    const wp::int32 var_214 = 1;
    wp::int32 var_215{};
    wp::vec_t<3, wp::float32>* var_216{};
    wp::vec_t<3, wp::float32> var_217{};
    wp::vec_t<3, wp::float32> var_218{};
    const wp::int32 var_219 = 1;
    wp::int32 var_220{};
    wp::float32* var_221{};
    wp::vec_t<3, wp::float32> var_222{};
    wp::float32 var_223{};
    wp::vec_t<3, wp::float32> var_224{};
    const wp::int32 var_225 = 1;
    wp::int32 var_226{};
    wp::float32* var_227{};
    wp::vec_t<3, wp::float32> var_228{};
    wp::float32 var_229{};
    wp::vec_t<3, wp::float32> var_230{};
    wp::vec_t<3, wp::float32> var_231{};
    wp::vec_t<3, wp::float32> var_232{};
    wp::vec_t<3, wp::float32> var_233{};
    const wp::int32 var_234 = 2;
    bool var_235{};
    const wp::int32 var_236 = 2;
    wp::int32 var_237{};
    wp::vec_t<3, wp::float32>* var_238{};
    wp::vec_t<3, wp::float32> var_239{};
    wp::vec_t<3, wp::float32> var_240{};
    const wp::int32 var_241 = 2;
    wp::int32 var_242{};
    wp::float32* var_243{};
    wp::vec_t<3, wp::float32> var_244{};
    wp::float32 var_245{};
    wp::vec_t<3, wp::float32> var_246{};
    const wp::int32 var_247 = 2;
    wp::int32 var_248{};
    wp::float32* var_249{};
    wp::vec_t<3, wp::float32> var_250{};
    wp::float32 var_251{};
    wp::vec_t<3, wp::float32> var_252{};
    wp::vec_t<3, wp::float32> var_253{};
    wp::vec_t<3, wp::float32> var_254{};
    wp::vec_t<3, wp::float32> var_255{};
    wp::int32 var_256{};
    wp::int32 var_257{};
    const wp::int32 var_258 = 1;
    bool var_259{};
    wp::vec_t<3, wp::float32>* var_260{};
    wp::vec_t<3, wp::float32> var_261{};
    wp::vec_t<3, wp::float32> var_262{};
    wp::float32* var_263{};
    wp::quat_t<wp::float32> var_264{};
    wp::float32 var_265{};
    wp::float32* var_266{};
    wp::vec_t<3, wp::float32> var_267{};
    wp::float32 var_268{};
    wp::vec_t<3, wp::float32> var_269{};
    wp::quat_t<wp::float32> var_270{};
    wp::vec_t<3, wp::float32> var_271{};
    const wp::int32 var_272 = 2;
    bool var_273{};
    const wp::int32 var_274 = 0;
    wp::int32 var_275{};
    wp::vec_t<3, wp::float32>* var_276{};
    const wp::int32 var_277 = 1;
    wp::int32 var_278{};
    wp::vec_t<3, wp::float32>* var_279{};
    const wp::int32 var_280 = 0;
    wp::int32 var_281{};
    wp::float32* var_282{};
    const wp::int32 var_283 = 1;
    wp::int32 var_284{};
    wp::float32* var_285{};
    const wp::int32 var_286 = 0;
    wp::int32 var_287{};
    wp::float32* var_288{};
    const wp::int32 var_289 = 1;
    wp::int32 var_290{};
    wp::float32* var_291{};
    wp::quat_t<wp::float32> var_292{};
    wp::vec_t<3, wp::float32> var_293{};
    wp::vec_t<3, wp::float32> var_294{};
    wp::vec_t<3, wp::float32> var_295{};
    wp::float32 var_296{};
    wp::float32 var_297{};
    wp::float32 var_298{};
    wp::float32 var_299{};
    wp::quat_t<wp::float32> var_300{};
    wp::vec_t<3, wp::float32> var_301{};
    const wp::int32 var_302 = 3;
    bool var_303{};
    const wp::int32 var_304 = 0;
    wp::int32 var_305{};
    wp::vec_t<3, wp::float32>* var_306{};
    const wp::int32 var_307 = 1;
    wp::int32 var_308{};
    wp::vec_t<3, wp::float32>* var_309{};
    const wp::int32 var_310 = 2;
    wp::int32 var_311{};
    wp::vec_t<3, wp::float32>* var_312{};
    const wp::int32 var_313 = 0;
    wp::int32 var_314{};
    wp::float32* var_315{};
    const wp::int32 var_316 = 1;
    wp::int32 var_317{};
    wp::float32* var_318{};
    const wp::int32 var_319 = 2;
    wp::int32 var_320{};
    wp::float32* var_321{};
    const wp::int32 var_322 = 0;
    wp::int32 var_323{};
    wp::float32* var_324{};
    const wp::int32 var_325 = 1;
    wp::int32 var_326{};
    wp::float32* var_327{};
    const wp::int32 var_328 = 2;
    wp::int32 var_329{};
    wp::float32* var_330{};
    wp::quat_t<wp::float32> var_331{};
    wp::vec_t<3, wp::float32> var_332{};
    wp::vec_t<3, wp::float32> var_333{};
    wp::vec_t<3, wp::float32> var_334{};
    wp::vec_t<3, wp::float32> var_335{};
    wp::float32 var_336{};
    wp::float32 var_337{};
    wp::float32 var_338{};
    wp::float32 var_339{};
    wp::float32 var_340{};
    wp::float32 var_341{};
    wp::quat_t<wp::float32> var_342{};
    wp::vec_t<3, wp::float32> var_343{};
    wp::transform_t<wp::float32> var_344{};
    wp::vec_t<6, wp::float32> var_345{};
    wp::transform_t<wp::float32> var_346{};
    wp::vec_t<6, wp::float32> var_347{};
    wp::vec_t<3, wp::float32> var_348{};
    wp::transform_t<wp::float32> var_349{};
    const wp::int32 var_350 = 0;
    bool var_351{};
    wp::transform_t<wp::float32>* var_352{};
    wp::transform_t<wp::float32> var_353{};
    wp::transform_t<wp::float32> var_354{};
    wp::transform_t<wp::float32> var_355{};
    wp::transform_t<wp::float32> var_356{};
    wp::transform_t<wp::float32> var_357{};
    wp::transform_t<wp::float32> var_358{};
    wp::transform_t<wp::float32> var_359{};
    wp::vec_t<3, wp::float32> var_360{};
    wp::vec_t<3, wp::float32> var_361{};
    wp::vec_t<3, wp::float32> var_362{};
    const wp::int32 var_363 = 0;
    bool var_364{};
    wp::vec_t<6, wp::float32>* var_365{};
    wp::vec_t<6, wp::float32> var_366{};
    wp::vec_t<6, wp::float32> var_367{};
    wp::vec_t<3, wp::float32> var_368{};
    wp::vec_t<3, wp::float32>* var_369{};
    wp::vec_t<3, wp::float32> var_370{};
    wp::vec_t<3, wp::float32> var_371{};
    wp::vec_t<3, wp::float32> var_372{};
    wp::vec_t<3, wp::float32> var_373{};
    wp::vec_t<3, wp::float32> var_374{};
    wp::vec_t<3, wp::float32> var_375{};
    wp::vec_t<3, wp::float32> var_376{};
    wp::vec_t<3, wp::float32> var_377{};
    bool var_378{};
    const wp::int32 var_379 = 4;
    bool var_380{};
    const wp::int32 var_381 = 5;
    bool var_382{};
    wp::vec_t<6, wp::float32> var_383{};
    wp::vec_t<3, wp::float32> var_384{};
    wp::vec_t<3, wp::float32> var_385{};
    wp::vec_t<3, wp::float32> var_386{};
    wp::vec_t<3, wp::float32> var_387{};
    wp::vec_t<3, wp::float32> var_388{};
    wp::vec_t<3, wp::float32> var_389{};
    wp::vec_t<3, wp::float32> var_390{};
    wp::vec_t<3, wp::float32> var_391{};
    wp::vec_t<3, wp::float32> var_392{};
    wp::vec_t<3, wp::float32> var_393{};
    wp::vec_t<6, wp::float32> var_394{};
    wp::vec_t<3, wp::float32>* var_395{};
    wp::vec_t<6, wp::float32> var_396{};
    wp::vec_t<3, wp::float32> var_397{};
    //---------
    // forward
    // def eval_single_articulation_fk_with_velocity_conversion(                              <L 1997>
    // for i in range(joint_start, joint_end):                                                <L 2016>
    var_0 = wp::range(var_joint_start, var_joint_end);
    start_for_0:;
        if (iter_cmp(var_0) == 0) goto end_for_0;
        var_1 = wp::iter_next(var_0);
        // parent = joint_parent[i]                                                           <L 2017>
        var_2 = wp::address(var_joint_parent, var_1);
        var_4 = wp::load(var_2);
        var_3 = wp::copy(var_4);
        // child = joint_child[i]                                                             <L 2018>
        var_5 = wp::address(var_joint_child, var_1);
        var_7 = wp::load(var_5);
        var_6 = wp::copy(var_7);
        // type = joint_type[i]                                                               <L 2021>
        var_8 = wp::address(var_joint_type, var_1);
        var_10 = wp::load(var_8);
        var_9 = wp::copy(var_10);
        // X_pj = joint_X_p[i]                                                                <L 2023>
        var_11 = wp::address(var_joint_X_p, var_1);
        var_13 = wp::load(var_11);
        var_12 = wp::copy(var_13);
        // X_cj = joint_X_c[i]                                                                <L 2024>
        var_14 = wp::address(var_joint_X_c, var_1);
        var_16 = wp::load(var_14);
        var_15 = wp::copy(var_16);
        // q_start = joint_q_start[i]                                                         <L 2026>
        var_17 = wp::address(var_joint_q_start, var_1);
        var_19 = wp::load(var_17);
        var_18 = wp::copy(var_19);
        // qd_start = joint_qd_start[i]                                                       <L 2027>
        var_20 = wp::address(var_joint_qd_start, var_1);
        var_22 = wp::load(var_20);
        var_21 = wp::copy(var_22);
        // lin_axis_count = joint_dof_dim[i, 0]                                               <L 2028>
        var_24 = wp::address(var_joint_dof_dim, var_1, var_23);
        var_26 = wp::load(var_24);
        var_25 = wp::copy(var_26);
        // ang_axis_count = joint_dof_dim[i, 1]                                               <L 2029>
        var_28 = wp::address(var_joint_dof_dim, var_1, var_27);
        var_30 = wp::load(var_28);
        var_29 = wp::copy(var_30);
        // X_j = wp.transform_identity()                                                      <L 2031>
        var_31 = wp::transform_identity<wp::float32>();
        // v_j = wp.spatial_vector(wp.vec3(), wp.vec3())                                      <L 2032>
        var_32 = wp::vec_t<3, wp::float32>();
        var_33 = wp::vec_t<3, wp::float32>();
        var_34 = wp::vec_t<6, wp::float32>(var_32, var_33);
        // if type == JointType.PRISMATIC:                                                    <L 2034>
        var_36 = (var_9 == var_35);
        if (var_36) {
            // axis = joint_axis[qd_start]                                                    <L 2035>
            var_37 = wp::address(var_joint_axis, var_21);
            var_39 = wp::load(var_37);
            var_38 = wp::copy(var_39);
            // q = joint_q[q_start]                                                           <L 2037>
            var_40 = wp::address(var_joint_q, var_18);
            var_42 = wp::load(var_40);
            var_41 = wp::copy(var_42);
            // qd = joint_qd[qd_start]                                                        <L 2038>
            var_43 = wp::address(var_joint_qd, var_21);
            var_45 = wp::load(var_43);
            var_44 = wp::copy(var_45);
            // X_j = wp.transform(axis * q, wp.quat_identity())                               <L 2040>
            var_46 = wp::mul(var_38, var_41);
            var_47 = wp::quat_identity<wp::float32>();
            var_48 = wp::transform_t<wp::float32>(var_46, var_47);
            // v_j = wp.spatial_vector(axis * qd, wp.vec3())                                  <L 2041>
            var_49 = wp::mul(var_38, var_44);
            var_50 = wp::vec_t<3, wp::float32>();
            var_51 = wp::vec_t<6, wp::float32>(var_49, var_50);
        }
        var_52 = wp::where(var_36, var_48, var_31);
        var_53 = wp::where(var_36, var_51, var_34);
        // if type == JointType.REVOLUTE:                                                     <L 2043>
        var_55 = (var_9 == var_54);
        if (var_55) {
            // axis = joint_axis[qd_start]                                                    <L 2044>
            var_56 = wp::address(var_joint_axis, var_21);
            var_58 = wp::load(var_56);
            var_57 = wp::copy(var_58);
            // q = joint_q[q_start]                                                           <L 2046>
            var_59 = wp::address(var_joint_q, var_18);
            var_61 = wp::load(var_59);
            var_60 = wp::copy(var_61);
            // qd = joint_qd[qd_start]                                                        <L 2047>
            var_62 = wp::address(var_joint_qd, var_21);
            var_64 = wp::load(var_62);
            var_63 = wp::copy(var_64);
            // X_j = wp.transform(wp.vec3(), wp.quat_from_axis_angle(axis, q))                <L 2049>
            var_65 = wp::vec_t<3, wp::float32>();
            var_66 = wp::quat_from_axis_angle(var_57, var_60);
            var_67 = wp::transform_t<wp::float32>(var_65, var_66);
            // v_j = wp.spatial_vector(wp.vec3(), axis * qd)                                  <L 2050>
            var_68 = wp::vec_t<3, wp::float32>();
            var_69 = wp::mul(var_57, var_63);
            var_70 = wp::vec_t<6, wp::float32>(var_68, var_69);
        }
        var_71 = wp::where(var_55, var_67, var_52);
        var_72 = wp::where(var_55, var_70, var_53);
        var_73 = wp::where(var_55, var_57, var_38);
        var_74 = wp::where(var_55, var_60, var_41);
        var_75 = wp::where(var_55, var_63, var_44);
        // if type == JointType.BALL:                                                         <L 2052>
        var_77 = (var_9 == var_76);
        if (var_77) {
            // r = wp.quat(joint_q[q_start + 0], joint_q[q_start + 1], joint_q[q_start + 2], joint_q[q_start + 3])       <L 2053>
            var_79 = wp::add(var_18, var_78);
            var_80 = wp::address(var_joint_q, var_79);
            var_82 = wp::add(var_18, var_81);
            var_83 = wp::address(var_joint_q, var_82);
            var_85 = wp::add(var_18, var_84);
            var_86 = wp::address(var_joint_q, var_85);
            var_88 = wp::add(var_18, var_87);
            var_89 = wp::address(var_joint_q, var_88);
            var_91 = wp::load(var_80);
            var_92 = wp::load(var_83);
            var_93 = wp::load(var_86);
            var_94 = wp::load(var_89);
            var_90 = wp::quat_t<wp::float32>(var_91, var_92, var_93, var_94);
            // w = wp.vec3(joint_qd[qd_start + 0], joint_qd[qd_start + 1], joint_qd[qd_start + 2])       <L 2055>
            var_96 = wp::add(var_21, var_95);
            var_97 = wp::address(var_joint_qd, var_96);
            var_99 = wp::add(var_21, var_98);
            var_100 = wp::address(var_joint_qd, var_99);
            var_102 = wp::add(var_21, var_101);
            var_103 = wp::address(var_joint_qd, var_102);
            var_105 = wp::load(var_97);
            var_106 = wp::load(var_100);
            var_107 = wp::load(var_103);
            var_104 = wp::vec_t<3, wp::float32>(var_105, var_106, var_107);
            // X_j = wp.transform(wp.vec3(), r)                                               <L 2057>
            var_108 = wp::vec_t<3, wp::float32>();
            var_109 = wp::transform_t<wp::float32>(var_108, var_90);
            // v_j = wp.spatial_vector(wp.vec3(), w)                                          <L 2058>
            var_110 = wp::vec_t<3, wp::float32>();
            var_111 = wp::vec_t<6, wp::float32>(var_110, var_104);
        }
        var_112 = wp::where(var_77, var_109, var_71);
        var_113 = wp::where(var_77, var_111, var_72);
        // if type == JointType.FREE or type == JointType.DISTANCE:                           <L 2060>
        var_116 = (var_9 == var_115);
        var_114 = var_116;
        if (!var_114) {
            var_118 = (var_9 == var_117);
            var_114 = var_114 || var_118;
        }
        if (var_114) {
            // t = wp.transform(                                                              <L 2061>
            // wp.vec3(joint_q[q_start + 0], joint_q[q_start + 1], joint_q[q_start + 2]),       <L 2062>
            var_120 = wp::add(var_18, var_119);
            var_121 = wp::address(var_joint_q, var_120);
            var_123 = wp::add(var_18, var_122);
            var_124 = wp::address(var_joint_q, var_123);
            var_126 = wp::add(var_18, var_125);
            var_127 = wp::address(var_joint_q, var_126);
            var_129 = wp::load(var_121);
            var_130 = wp::load(var_124);
            var_131 = wp::load(var_127);
            var_128 = wp::vec_t<3, wp::float32>(var_129, var_130, var_131);
            // wp.quat(joint_q[q_start + 3], joint_q[q_start + 4], joint_q[q_start + 5], joint_q[q_start + 6]),       <L 2063>
            var_133 = wp::add(var_18, var_132);
            var_134 = wp::address(var_joint_q, var_133);
            var_136 = wp::add(var_18, var_135);
            var_137 = wp::address(var_joint_q, var_136);
            var_139 = wp::add(var_18, var_138);
            var_140 = wp::address(var_joint_q, var_139);
            var_142 = wp::add(var_18, var_141);
            var_143 = wp::address(var_joint_q, var_142);
            var_145 = wp::load(var_134);
            var_146 = wp::load(var_137);
            var_147 = wp::load(var_140);
            var_148 = wp::load(var_143);
            var_144 = wp::quat_t<wp::float32>(var_145, var_146, var_147, var_148);
            var_149 = wp::transform_t<wp::float32>(var_128, var_144);
            // v = wp.spatial_vector(                                                         <L 2066>
            // wp.vec3(joint_qd[qd_start + 0], joint_qd[qd_start + 1], joint_qd[qd_start + 2]),       <L 2067>
            var_151 = wp::add(var_21, var_150);
            var_152 = wp::address(var_joint_qd, var_151);
            var_154 = wp::add(var_21, var_153);
            var_155 = wp::address(var_joint_qd, var_154);
            var_157 = wp::add(var_21, var_156);
            var_158 = wp::address(var_joint_qd, var_157);
            var_160 = wp::load(var_152);
            var_161 = wp::load(var_155);
            var_162 = wp::load(var_158);
            var_159 = wp::vec_t<3, wp::float32>(var_160, var_161, var_162);
            // wp.vec3(joint_qd[qd_start + 3], joint_qd[qd_start + 4], joint_qd[qd_start + 5]),       <L 2068>
            var_164 = wp::add(var_21, var_163);
            var_165 = wp::address(var_joint_qd, var_164);
            var_167 = wp::add(var_21, var_166);
            var_168 = wp::address(var_joint_qd, var_167);
            var_170 = wp::add(var_21, var_169);
            var_171 = wp::address(var_joint_qd, var_170);
            var_173 = wp::load(var_165);
            var_174 = wp::load(var_168);
            var_175 = wp::load(var_171);
            var_172 = wp::vec_t<3, wp::float32>(var_173, var_174, var_175);
            var_176 = wp::vec_t<6, wp::float32>(var_159, var_172);
            // X_j = t                                                                        <L 2071>
            var_177 = wp::copy(var_149);
            // v_j = v                                                                        <L 2072>
            var_178 = wp::copy(var_176);
        }
        var_179 = wp::where(var_114, var_177, var_112);
        var_180 = wp::where(var_114, var_178, var_113);
        // if type == JointType.D6:                                                           <L 2074>
        var_182 = (var_9 == var_181);
        if (var_182) {
            // pos = wp.vec3(0.0)                                                             <L 2075>
            var_184 = wp::vec_t<3, wp::float32>(var_183);
            // rot = wp.quat_identity()                                                       <L 2076>
            var_185 = wp::quat_identity<wp::float32>();
            // vel_v = wp.vec3(0.0)                                                           <L 2077>
            var_187 = wp::vec_t<3, wp::float32>(var_186);
            // vel_w = wp.vec3(0.0)                                                           <L 2078>
            var_189 = wp::vec_t<3, wp::float32>(var_188);
            // if lin_axis_count > 0:                                                         <L 2083>
            var_191 = (var_25 > var_190);
            if (var_191) {
                // axis = joint_axis[qd_start + 0]                                            <L 2084>
                var_193 = wp::add(var_21, var_192);
                var_194 = wp::address(var_joint_axis, var_193);
                var_196 = wp::load(var_194);
                var_195 = wp::copy(var_196);
                // pos += axis * joint_q[q_start + 0]                                         <L 2085>
                var_198 = wp::add(var_18, var_197);
                var_199 = wp::address(var_joint_q, var_198);
                var_201 = wp::load(var_199);
                var_200 = wp::mul(var_195, var_201);
                var_202 = wp::add(var_184, var_200);
                // vel_v += axis * joint_qd[qd_start + 0]                                     <L 2086>
                var_204 = wp::add(var_21, var_203);
                var_205 = wp::address(var_joint_qd, var_204);
                var_207 = wp::load(var_205);
                var_206 = wp::mul(var_195, var_207);
                var_208 = wp::add(var_187, var_206);
            }
            var_209 = wp::where(var_191, var_195, var_73);
            var_210 = wp::where(var_191, var_202, var_184);
            var_211 = wp::where(var_191, var_208, var_187);
            // if lin_axis_count > 1:                                                         <L 2087>
            var_213 = (var_25 > var_212);
            if (var_213) {
                // axis = joint_axis[qd_start + 1]                                            <L 2088>
                var_215 = wp::add(var_21, var_214);
                var_216 = wp::address(var_joint_axis, var_215);
                var_218 = wp::load(var_216);
                var_217 = wp::copy(var_218);
                // pos += axis * joint_q[q_start + 1]                                         <L 2089>
                var_220 = wp::add(var_18, var_219);
                var_221 = wp::address(var_joint_q, var_220);
                var_223 = wp::load(var_221);
                var_222 = wp::mul(var_217, var_223);
                var_224 = wp::add(var_210, var_222);
                // vel_v += axis * joint_qd[qd_start + 1]                                     <L 2090>
                var_226 = wp::add(var_21, var_225);
                var_227 = wp::address(var_joint_qd, var_226);
                var_229 = wp::load(var_227);
                var_228 = wp::mul(var_217, var_229);
                var_230 = wp::add(var_211, var_228);
            }
            var_231 = wp::where(var_213, var_217, var_209);
            var_232 = wp::where(var_213, var_224, var_210);
            var_233 = wp::where(var_213, var_230, var_211);
            // if lin_axis_count > 2:                                                         <L 2091>
            var_235 = (var_25 > var_234);
            if (var_235) {
                // axis = joint_axis[qd_start + 2]                                            <L 2092>
                var_237 = wp::add(var_21, var_236);
                var_238 = wp::address(var_joint_axis, var_237);
                var_240 = wp::load(var_238);
                var_239 = wp::copy(var_240);
                // pos += axis * joint_q[q_start + 2]                                         <L 2093>
                var_242 = wp::add(var_18, var_241);
                var_243 = wp::address(var_joint_q, var_242);
                var_245 = wp::load(var_243);
                var_244 = wp::mul(var_239, var_245);
                var_246 = wp::add(var_232, var_244);
                // vel_v += axis * joint_qd[qd_start + 2]                                     <L 2094>
                var_248 = wp::add(var_21, var_247);
                var_249 = wp::address(var_joint_qd, var_248);
                var_251 = wp::load(var_249);
                var_250 = wp::mul(var_239, var_251);
                var_252 = wp::add(var_233, var_250);
            }
            var_253 = wp::where(var_235, var_239, var_231);
            var_254 = wp::where(var_235, var_246, var_232);
            var_255 = wp::where(var_235, var_252, var_233);
            // iq = q_start + lin_axis_count                                                  <L 2096>
            var_256 = wp::add(var_18, var_25);
            // iqd = qd_start + lin_axis_count                                                <L 2097>
            var_257 = wp::add(var_21, var_25);
            // if ang_axis_count == 1:                                                        <L 2098>
            var_259 = (var_29 == var_258);
            if (var_259) {
                // axis = joint_axis[iqd]                                                     <L 2099>
                var_260 = wp::address(var_joint_axis, var_257);
                var_262 = wp::load(var_260);
                var_261 = wp::copy(var_262);
                // rot = wp.quat_from_axis_angle(axis, joint_q[iq])                           <L 2100>
                var_263 = wp::address(var_joint_q, var_256);
                var_265 = wp::load(var_263);
                var_264 = wp::quat_from_axis_angle(var_261, var_265);
                // vel_w = joint_qd[iqd] * axis                                               <L 2101>
                var_266 = wp::address(var_joint_qd, var_257);
                var_268 = wp::load(var_266);
                var_267 = wp::mul(var_268, var_261);
            }
            var_269 = wp::where(var_259, var_261, var_253);
            var_270 = wp::where(var_259, var_264, var_185);
            var_271 = wp::where(var_259, var_267, var_189);
            // if ang_axis_count == 2:                                                        <L 2102>
            var_273 = (var_29 == var_272);
            if (var_273) {
                // rot, vel_w = compute_2d_rotational_dofs(                                   <L 2103>
                // joint_axis[iqd + 0],                                                       <L 2104>
                var_275 = wp::add(var_257, var_274);
                var_276 = wp::address(var_joint_axis, var_275);
                // joint_axis[iqd + 1],                                                       <L 2105>
                var_278 = wp::add(var_257, var_277);
                var_279 = wp::address(var_joint_axis, var_278);
                // joint_q[iq + 0],                                                           <L 2106>
                var_281 = wp::add(var_256, var_280);
                var_282 = wp::address(var_joint_q, var_281);
                // joint_q[iq + 1],                                                           <L 2107>
                var_284 = wp::add(var_256, var_283);
                var_285 = wp::address(var_joint_q, var_284);
                // joint_qd[iqd + 0],                                                         <L 2108>
                var_287 = wp::add(var_257, var_286);
                var_288 = wp::address(var_joint_qd, var_287);
                // joint_qd[iqd + 1],                                                         <L 2109>
                var_290 = wp::add(var_257, var_289);
                var_291 = wp::address(var_joint_qd, var_290);
                var_294 = wp::load(var_276);
                var_295 = wp::load(var_279);
                var_296 = wp::load(var_282);
                var_297 = wp::load(var_285);
                var_298 = wp::load(var_288);
                var_299 = wp::load(var_291);
                compute_2d_rotational_dofs_0(var_294, var_295, var_296, var_297, var_298, var_299, var_292, var_293);
            }
            var_300 = wp::where(var_273, var_292, var_270);
            var_301 = wp::where(var_273, var_293, var_271);
            // if ang_axis_count == 3:                                                        <L 2111>
            var_303 = (var_29 == var_302);
            if (var_303) {
                // rot, vel_w = compute_3d_rotational_dofs(                                   <L 2112>
                // joint_axis[iqd + 0],                                                       <L 2113>
                var_305 = wp::add(var_257, var_304);
                var_306 = wp::address(var_joint_axis, var_305);
                // joint_axis[iqd + 1],                                                       <L 2114>
                var_308 = wp::add(var_257, var_307);
                var_309 = wp::address(var_joint_axis, var_308);
                // joint_axis[iqd + 2],                                                       <L 2115>
                var_311 = wp::add(var_257, var_310);
                var_312 = wp::address(var_joint_axis, var_311);
                // joint_q[iq + 0],                                                           <L 2116>
                var_314 = wp::add(var_256, var_313);
                var_315 = wp::address(var_joint_q, var_314);
                // joint_q[iq + 1],                                                           <L 2117>
                var_317 = wp::add(var_256, var_316);
                var_318 = wp::address(var_joint_q, var_317);
                // joint_q[iq + 2],                                                           <L 2118>
                var_320 = wp::add(var_256, var_319);
                var_321 = wp::address(var_joint_q, var_320);
                // joint_qd[iqd + 0],                                                         <L 2119>
                var_323 = wp::add(var_257, var_322);
                var_324 = wp::address(var_joint_qd, var_323);
                // joint_qd[iqd + 1],                                                         <L 2120>
                var_326 = wp::add(var_257, var_325);
                var_327 = wp::address(var_joint_qd, var_326);
                // joint_qd[iqd + 2],                                                         <L 2121>
                var_329 = wp::add(var_257, var_328);
                var_330 = wp::address(var_joint_qd, var_329);
                var_333 = wp::load(var_306);
                var_334 = wp::load(var_309);
                var_335 = wp::load(var_312);
                var_336 = wp::load(var_315);
                var_337 = wp::load(var_318);
                var_338 = wp::load(var_321);
                var_339 = wp::load(var_324);
                var_340 = wp::load(var_327);
                var_341 = wp::load(var_330);
                compute_3d_rotational_dofs_0(var_333, var_334, var_335, var_336, var_337, var_338, var_339, var_340, var_341, var_331, var_332);
            }
            var_342 = wp::where(var_303, var_331, var_300);
            var_343 = wp::where(var_303, var_332, var_301);
            // X_j = wp.transform(pos, rot)                                                   <L 2124>
            var_344 = wp::transform_t<wp::float32>(var_254, var_342);
            // v_j = wp.spatial_vector(vel_v, vel_w)                                          <L 2125>
            var_345 = wp::vec_t<6, wp::float32>(var_255, var_343);
        }
        var_346 = wp::where(var_182, var_344, var_179);
        var_347 = wp::where(var_182, var_345, var_180);
        var_348 = wp::where(var_182, var_269, var_73);
        // X_wpj = X_pj                                                                       <L 2128>
        var_349 = wp::copy(var_12);
        // if parent >= 0:                                                                    <L 2129>
        var_351 = (var_3 >= var_350);
        if (var_351) {
            // X_wp = body_q[parent]                                                          <L 2130>
            var_352 = wp::address(var_body_q, var_3);
            var_354 = wp::load(var_352);
            var_353 = wp::copy(var_354);
            // X_wpj = X_wp * X_wpj                                                           <L 2131>
            var_355 = wp::mul(var_353, var_349);
        }
        var_356 = wp::where(var_351, var_355, var_349);
        // X_wcj = X_wpj * X_j                                                                <L 2134>
        var_357 = wp::mul(var_356, var_346);
        // X_wc = X_wcj * wp.transform_inverse(X_cj)                                          <L 2136>
        var_358 = wp::transform_inverse(var_15);
        var_359 = wp::mul(var_357, var_358);
        // x_child_origin = wp.transform_get_translation(X_wc)                                <L 2138>
        var_360 = wp::transform_get_translation(var_359);
        // v_parent_origin = wp.vec3()                                                        <L 2139>
        var_361 = wp::vec_t<3, wp::float32>();
        // w_parent = wp.vec3()                                                               <L 2140>
        var_362 = wp::vec_t<3, wp::float32>();
        // if parent >= 0:                                                                    <L 2141>
        var_364 = (var_3 >= var_363);
        if (var_364) {
            // v_wp = body_qd[parent]                                                         <L 2142>
            var_365 = wp::address(var_body_qd, var_3);
            var_367 = wp::load(var_365);
            var_366 = wp::copy(var_367);
            // w_parent = wp.spatial_bottom(v_wp)                                             <L 2143>
            var_368 = wp::spatial_bottom(var_366);
            // v_parent_origin = com_twist_to_point_velocity(v_wp, X_wp, body_com[parent], x_child_origin)       <L 2144>
            var_369 = wp::address(var_body_com, var_3);
            var_371 = wp::load(var_369);
            var_370 = com_twist_to_point_velocity_0(var_366, var_353, var_371, var_360);
        }
        var_372 = wp::where(var_364, var_370, var_361);
        var_373 = wp::where(var_364, var_368, var_362);
        // linear_joint_world = wp.transform_vector(X_wpj, wp.spatial_top(v_j))               <L 2145>
        var_374 = wp::spatial_top(var_347);
        var_375 = wp::transform_vector(var_356, var_374);
        // angular_joint_world = wp.transform_vector(X_wpj, wp.spatial_bottom(v_j))           <L 2146>
        var_376 = wp::spatial_bottom(var_347);
        var_377 = wp::transform_vector(var_356, var_376);
        // if type == JointType.FREE or type == JointType.DISTANCE:                           <L 2147>
        var_380 = (var_9 == var_379);
        var_378 = var_380;
        if (!var_378) {
            var_382 = (var_9 == var_381);
            var_378 = var_378 || var_382;
        }
        if (var_378) {
            // v_j_world = transform_twist(X_wpj, v_j)                                        <L 2148>
            var_383 = transform_twist_1(var_356, var_347);
            // linear_joint_origin = velocity_at_point(v_j_world, x_child_origin)             <L 2149>
            var_384 = velocity_at_point_1(var_383, var_360);
            // angular_joint_world = wp.spatial_bottom(v_j_world)                             <L 2150>
            var_385 = wp::spatial_bottom(var_383);
        }
        if (!var_378) {
            // child_origin_offset_world = x_child_origin - wp.transform_get_translation(X_wcj)       <L 2152>
            var_386 = wp::transform_get_translation(var_357);
            var_387 = wp::sub(var_360, var_386);
            // linear_joint_origin = linear_joint_world + wp.cross(angular_joint_world, child_origin_offset_world)       <L 2153>
            var_388 = wp::cross(var_377, var_387);
            var_389 = wp::add(var_375, var_388);
        }
        var_390 = wp::where(var_378, var_385, var_377);
        var_391 = wp::where(var_378, var_384, var_389);
        // v_wc_origin = wp.spatial_vector(v_parent_origin + linear_joint_origin, w_parent + angular_joint_world)       <L 2155>
        var_392 = wp::add(var_372, var_391);
        var_393 = wp::add(var_373, var_390);
        var_394 = wp::vec_t<6, wp::float32>(var_392, var_393);
        // body_q[child] = X_wc                                                               <L 2157>
        wp::array_store(var_body_q, var_6, var_359);
        // body_qd[child] = origin_twist_to_com_twist(v_wc_origin, X_wc, body_com[child])       <L 2158>
        var_395 = wp::address(var_body_com, var_6);
        var_397 = wp::load(var_395);
        var_396 = origin_twist_to_com_twist_0(var_394, var_359, var_397);
        wp::array_store(var_body_qd, var_6, var_396);
        goto start_for_0;
    end_for_0:;
}


// newton/_src/solvers/featherstone/kernels.py:1545
static void spatial_mass_0(
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_s,
    wp::int32 var_joint_start,
    wp::int32 var_joint_count,
    wp::int32 var_M_start,
    wp::array_t<wp::float32> var_M)
{
    //---------
    // primal vars
    const wp::int32 var_0 = 6;
    wp::int32 var_1{};
    wp::range_t var_2{};
    wp::int32 var_3{};
    wp::int32 var_4{};
    wp::mat_t<6, 6, wp::float32>* var_5{};
    wp::mat_t<6, 6, wp::float32> var_6{};
    wp::mat_t<6, 6, wp::float32> var_7{};
    const wp::int32 var_8 = 0;
    const wp::int32 var_9 = 0;
    wp::float32 var_10{};
    const wp::int32 var_11 = 6;
    wp::int32 var_12{};
    wp::int32 var_13{};
    const wp::int32 var_14 = 6;
    wp::int32 var_15{};
    wp::int32 var_16{};
    wp::int32 var_17{};
    wp::int32 var_18{};
    const wp::int32 var_19 = 1;
    wp::float32 var_20{};
    const wp::int32 var_21 = 6;
    wp::int32 var_22{};
    wp::int32 var_23{};
    const wp::int32 var_24 = 6;
    wp::int32 var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    wp::int32 var_28{};
    const wp::int32 var_29 = 2;
    wp::float32 var_30{};
    const wp::int32 var_31 = 6;
    wp::int32 var_32{};
    wp::int32 var_33{};
    const wp::int32 var_34 = 6;
    wp::int32 var_35{};
    wp::int32 var_36{};
    wp::int32 var_37{};
    wp::int32 var_38{};
    const wp::int32 var_39 = 3;
    wp::float32 var_40{};
    const wp::int32 var_41 = 6;
    wp::int32 var_42{};
    wp::int32 var_43{};
    const wp::int32 var_44 = 6;
    wp::int32 var_45{};
    wp::int32 var_46{};
    wp::int32 var_47{};
    wp::int32 var_48{};
    const wp::int32 var_49 = 4;
    wp::float32 var_50{};
    const wp::int32 var_51 = 6;
    wp::int32 var_52{};
    wp::int32 var_53{};
    const wp::int32 var_54 = 6;
    wp::int32 var_55{};
    wp::int32 var_56{};
    wp::int32 var_57{};
    wp::int32 var_58{};
    const wp::int32 var_59 = 5;
    wp::float32 var_60{};
    const wp::int32 var_61 = 6;
    wp::int32 var_62{};
    wp::int32 var_63{};
    const wp::int32 var_64 = 6;
    wp::int32 var_65{};
    wp::int32 var_66{};
    wp::int32 var_67{};
    wp::int32 var_68{};
    const wp::int32 var_69 = 1;
    const wp::int32 var_70 = 0;
    wp::float32 var_71{};
    const wp::int32 var_72 = 6;
    wp::int32 var_73{};
    wp::int32 var_74{};
    const wp::int32 var_75 = 6;
    wp::int32 var_76{};
    wp::int32 var_77{};
    wp::int32 var_78{};
    wp::int32 var_79{};
    const wp::int32 var_80 = 1;
    wp::float32 var_81{};
    const wp::int32 var_82 = 6;
    wp::int32 var_83{};
    wp::int32 var_84{};
    const wp::int32 var_85 = 6;
    wp::int32 var_86{};
    wp::int32 var_87{};
    wp::int32 var_88{};
    wp::int32 var_89{};
    const wp::int32 var_90 = 2;
    wp::float32 var_91{};
    const wp::int32 var_92 = 6;
    wp::int32 var_93{};
    wp::int32 var_94{};
    const wp::int32 var_95 = 6;
    wp::int32 var_96{};
    wp::int32 var_97{};
    wp::int32 var_98{};
    wp::int32 var_99{};
    const wp::int32 var_100 = 3;
    wp::float32 var_101{};
    const wp::int32 var_102 = 6;
    wp::int32 var_103{};
    wp::int32 var_104{};
    const wp::int32 var_105 = 6;
    wp::int32 var_106{};
    wp::int32 var_107{};
    wp::int32 var_108{};
    wp::int32 var_109{};
    const wp::int32 var_110 = 4;
    wp::float32 var_111{};
    const wp::int32 var_112 = 6;
    wp::int32 var_113{};
    wp::int32 var_114{};
    const wp::int32 var_115 = 6;
    wp::int32 var_116{};
    wp::int32 var_117{};
    wp::int32 var_118{};
    wp::int32 var_119{};
    const wp::int32 var_120 = 5;
    wp::float32 var_121{};
    const wp::int32 var_122 = 6;
    wp::int32 var_123{};
    wp::int32 var_124{};
    const wp::int32 var_125 = 6;
    wp::int32 var_126{};
    wp::int32 var_127{};
    wp::int32 var_128{};
    wp::int32 var_129{};
    const wp::int32 var_130 = 2;
    const wp::int32 var_131 = 0;
    wp::float32 var_132{};
    const wp::int32 var_133 = 6;
    wp::int32 var_134{};
    wp::int32 var_135{};
    const wp::int32 var_136 = 6;
    wp::int32 var_137{};
    wp::int32 var_138{};
    wp::int32 var_139{};
    wp::int32 var_140{};
    const wp::int32 var_141 = 1;
    wp::float32 var_142{};
    const wp::int32 var_143 = 6;
    wp::int32 var_144{};
    wp::int32 var_145{};
    const wp::int32 var_146 = 6;
    wp::int32 var_147{};
    wp::int32 var_148{};
    wp::int32 var_149{};
    wp::int32 var_150{};
    const wp::int32 var_151 = 2;
    wp::float32 var_152{};
    const wp::int32 var_153 = 6;
    wp::int32 var_154{};
    wp::int32 var_155{};
    const wp::int32 var_156 = 6;
    wp::int32 var_157{};
    wp::int32 var_158{};
    wp::int32 var_159{};
    wp::int32 var_160{};
    const wp::int32 var_161 = 3;
    wp::float32 var_162{};
    const wp::int32 var_163 = 6;
    wp::int32 var_164{};
    wp::int32 var_165{};
    const wp::int32 var_166 = 6;
    wp::int32 var_167{};
    wp::int32 var_168{};
    wp::int32 var_169{};
    wp::int32 var_170{};
    const wp::int32 var_171 = 4;
    wp::float32 var_172{};
    const wp::int32 var_173 = 6;
    wp::int32 var_174{};
    wp::int32 var_175{};
    const wp::int32 var_176 = 6;
    wp::int32 var_177{};
    wp::int32 var_178{};
    wp::int32 var_179{};
    wp::int32 var_180{};
    const wp::int32 var_181 = 5;
    wp::float32 var_182{};
    const wp::int32 var_183 = 6;
    wp::int32 var_184{};
    wp::int32 var_185{};
    const wp::int32 var_186 = 6;
    wp::int32 var_187{};
    wp::int32 var_188{};
    wp::int32 var_189{};
    wp::int32 var_190{};
    const wp::int32 var_191 = 3;
    const wp::int32 var_192 = 0;
    wp::float32 var_193{};
    const wp::int32 var_194 = 6;
    wp::int32 var_195{};
    wp::int32 var_196{};
    const wp::int32 var_197 = 6;
    wp::int32 var_198{};
    wp::int32 var_199{};
    wp::int32 var_200{};
    wp::int32 var_201{};
    const wp::int32 var_202 = 1;
    wp::float32 var_203{};
    const wp::int32 var_204 = 6;
    wp::int32 var_205{};
    wp::int32 var_206{};
    const wp::int32 var_207 = 6;
    wp::int32 var_208{};
    wp::int32 var_209{};
    wp::int32 var_210{};
    wp::int32 var_211{};
    const wp::int32 var_212 = 2;
    wp::float32 var_213{};
    const wp::int32 var_214 = 6;
    wp::int32 var_215{};
    wp::int32 var_216{};
    const wp::int32 var_217 = 6;
    wp::int32 var_218{};
    wp::int32 var_219{};
    wp::int32 var_220{};
    wp::int32 var_221{};
    const wp::int32 var_222 = 3;
    wp::float32 var_223{};
    const wp::int32 var_224 = 6;
    wp::int32 var_225{};
    wp::int32 var_226{};
    const wp::int32 var_227 = 6;
    wp::int32 var_228{};
    wp::int32 var_229{};
    wp::int32 var_230{};
    wp::int32 var_231{};
    const wp::int32 var_232 = 4;
    wp::float32 var_233{};
    const wp::int32 var_234 = 6;
    wp::int32 var_235{};
    wp::int32 var_236{};
    const wp::int32 var_237 = 6;
    wp::int32 var_238{};
    wp::int32 var_239{};
    wp::int32 var_240{};
    wp::int32 var_241{};
    const wp::int32 var_242 = 5;
    wp::float32 var_243{};
    const wp::int32 var_244 = 6;
    wp::int32 var_245{};
    wp::int32 var_246{};
    const wp::int32 var_247 = 6;
    wp::int32 var_248{};
    wp::int32 var_249{};
    wp::int32 var_250{};
    wp::int32 var_251{};
    const wp::int32 var_252 = 4;
    const wp::int32 var_253 = 0;
    wp::float32 var_254{};
    const wp::int32 var_255 = 6;
    wp::int32 var_256{};
    wp::int32 var_257{};
    const wp::int32 var_258 = 6;
    wp::int32 var_259{};
    wp::int32 var_260{};
    wp::int32 var_261{};
    wp::int32 var_262{};
    const wp::int32 var_263 = 1;
    wp::float32 var_264{};
    const wp::int32 var_265 = 6;
    wp::int32 var_266{};
    wp::int32 var_267{};
    const wp::int32 var_268 = 6;
    wp::int32 var_269{};
    wp::int32 var_270{};
    wp::int32 var_271{};
    wp::int32 var_272{};
    const wp::int32 var_273 = 2;
    wp::float32 var_274{};
    const wp::int32 var_275 = 6;
    wp::int32 var_276{};
    wp::int32 var_277{};
    const wp::int32 var_278 = 6;
    wp::int32 var_279{};
    wp::int32 var_280{};
    wp::int32 var_281{};
    wp::int32 var_282{};
    const wp::int32 var_283 = 3;
    wp::float32 var_284{};
    const wp::int32 var_285 = 6;
    wp::int32 var_286{};
    wp::int32 var_287{};
    const wp::int32 var_288 = 6;
    wp::int32 var_289{};
    wp::int32 var_290{};
    wp::int32 var_291{};
    wp::int32 var_292{};
    const wp::int32 var_293 = 4;
    wp::float32 var_294{};
    const wp::int32 var_295 = 6;
    wp::int32 var_296{};
    wp::int32 var_297{};
    const wp::int32 var_298 = 6;
    wp::int32 var_299{};
    wp::int32 var_300{};
    wp::int32 var_301{};
    wp::int32 var_302{};
    const wp::int32 var_303 = 5;
    wp::float32 var_304{};
    const wp::int32 var_305 = 6;
    wp::int32 var_306{};
    wp::int32 var_307{};
    const wp::int32 var_308 = 6;
    wp::int32 var_309{};
    wp::int32 var_310{};
    wp::int32 var_311{};
    wp::int32 var_312{};
    const wp::int32 var_313 = 5;
    const wp::int32 var_314 = 0;
    wp::float32 var_315{};
    const wp::int32 var_316 = 6;
    wp::int32 var_317{};
    wp::int32 var_318{};
    const wp::int32 var_319 = 6;
    wp::int32 var_320{};
    wp::int32 var_321{};
    wp::int32 var_322{};
    wp::int32 var_323{};
    const wp::int32 var_324 = 1;
    wp::float32 var_325{};
    const wp::int32 var_326 = 6;
    wp::int32 var_327{};
    wp::int32 var_328{};
    const wp::int32 var_329 = 6;
    wp::int32 var_330{};
    wp::int32 var_331{};
    wp::int32 var_332{};
    wp::int32 var_333{};
    const wp::int32 var_334 = 2;
    wp::float32 var_335{};
    const wp::int32 var_336 = 6;
    wp::int32 var_337{};
    wp::int32 var_338{};
    const wp::int32 var_339 = 6;
    wp::int32 var_340{};
    wp::int32 var_341{};
    wp::int32 var_342{};
    wp::int32 var_343{};
    const wp::int32 var_344 = 3;
    wp::float32 var_345{};
    const wp::int32 var_346 = 6;
    wp::int32 var_347{};
    wp::int32 var_348{};
    const wp::int32 var_349 = 6;
    wp::int32 var_350{};
    wp::int32 var_351{};
    wp::int32 var_352{};
    wp::int32 var_353{};
    const wp::int32 var_354 = 4;
    wp::float32 var_355{};
    const wp::int32 var_356 = 6;
    wp::int32 var_357{};
    wp::int32 var_358{};
    const wp::int32 var_359 = 6;
    wp::int32 var_360{};
    wp::int32 var_361{};
    wp::int32 var_362{};
    wp::int32 var_363{};
    const wp::int32 var_364 = 5;
    wp::float32 var_365{};
    const wp::int32 var_366 = 6;
    wp::int32 var_367{};
    wp::int32 var_368{};
    const wp::int32 var_369 = 6;
    wp::int32 var_370{};
    wp::int32 var_371{};
    wp::int32 var_372{};
    wp::int32 var_373{};
    //---------
    // forward
    // def spatial_mass(                                                                      <L 1546>
    // stride = joint_count * 6                                                               <L 1554>
    var_1 = wp::mul(var_joint_count, var_0);
    // for l in range(joint_count):                                                           <L 1555>
    var_2 = wp::range(var_joint_count);
    start_for_0:;
        if (iter_cmp(var_2) == 0) goto end_for_0;
        var_3 = wp::iter_next(var_2);
        // I = body_I_s[joint_start + l]                                                      <L 1556>
        var_4 = wp::add(var_joint_start, var_3);
        var_5 = wp::address(var_body_I_s, var_4);
        var_7 = wp::load(var_5);
        var_6 = wp::copy(var_7);
        // for i in range(6):                                                                 <L 1557>
        // for j in range(6):                                                                 <L 1558>
        // M[M_start + dense_index(stride, l * 6 + i, l * 6 + j)] = I[i, j]                   <L 1559>
        var_10 = wp::extract(var_6, var_8, var_9);
        var_12 = wp::mul(var_3, var_11);
        var_13 = wp::add(var_12, var_8);
        var_15 = wp::mul(var_3, var_14);
        var_16 = wp::add(var_15, var_9);
        var_17 = dense_index_0(var_1, var_13, var_16);
        var_18 = wp::add(var_M_start, var_17);
        wp::array_store(var_M, var_18, var_10);
        var_20 = wp::extract(var_6, var_8, var_19);
        var_22 = wp::mul(var_3, var_21);
        var_23 = wp::add(var_22, var_8);
        var_25 = wp::mul(var_3, var_24);
        var_26 = wp::add(var_25, var_19);
        var_27 = dense_index_0(var_1, var_23, var_26);
        var_28 = wp::add(var_M_start, var_27);
        wp::array_store(var_M, var_28, var_20);
        var_30 = wp::extract(var_6, var_8, var_29);
        var_32 = wp::mul(var_3, var_31);
        var_33 = wp::add(var_32, var_8);
        var_35 = wp::mul(var_3, var_34);
        var_36 = wp::add(var_35, var_29);
        var_37 = dense_index_0(var_1, var_33, var_36);
        var_38 = wp::add(var_M_start, var_37);
        wp::array_store(var_M, var_38, var_30);
        var_40 = wp::extract(var_6, var_8, var_39);
        var_42 = wp::mul(var_3, var_41);
        var_43 = wp::add(var_42, var_8);
        var_45 = wp::mul(var_3, var_44);
        var_46 = wp::add(var_45, var_39);
        var_47 = dense_index_0(var_1, var_43, var_46);
        var_48 = wp::add(var_M_start, var_47);
        wp::array_store(var_M, var_48, var_40);
        var_50 = wp::extract(var_6, var_8, var_49);
        var_52 = wp::mul(var_3, var_51);
        var_53 = wp::add(var_52, var_8);
        var_55 = wp::mul(var_3, var_54);
        var_56 = wp::add(var_55, var_49);
        var_57 = dense_index_0(var_1, var_53, var_56);
        var_58 = wp::add(var_M_start, var_57);
        wp::array_store(var_M, var_58, var_50);
        var_60 = wp::extract(var_6, var_8, var_59);
        var_62 = wp::mul(var_3, var_61);
        var_63 = wp::add(var_62, var_8);
        var_65 = wp::mul(var_3, var_64);
        var_66 = wp::add(var_65, var_59);
        var_67 = dense_index_0(var_1, var_63, var_66);
        var_68 = wp::add(var_M_start, var_67);
        wp::array_store(var_M, var_68, var_60);
        // for j in range(6):                                                                 <L 1558>
        // M[M_start + dense_index(stride, l * 6 + i, l * 6 + j)] = I[i, j]                   <L 1559>
        var_71 = wp::extract(var_6, var_69, var_70);
        var_73 = wp::mul(var_3, var_72);
        var_74 = wp::add(var_73, var_69);
        var_76 = wp::mul(var_3, var_75);
        var_77 = wp::add(var_76, var_70);
        var_78 = dense_index_0(var_1, var_74, var_77);
        var_79 = wp::add(var_M_start, var_78);
        wp::array_store(var_M, var_79, var_71);
        var_81 = wp::extract(var_6, var_69, var_80);
        var_83 = wp::mul(var_3, var_82);
        var_84 = wp::add(var_83, var_69);
        var_86 = wp::mul(var_3, var_85);
        var_87 = wp::add(var_86, var_80);
        var_88 = dense_index_0(var_1, var_84, var_87);
        var_89 = wp::add(var_M_start, var_88);
        wp::array_store(var_M, var_89, var_81);
        var_91 = wp::extract(var_6, var_69, var_90);
        var_93 = wp::mul(var_3, var_92);
        var_94 = wp::add(var_93, var_69);
        var_96 = wp::mul(var_3, var_95);
        var_97 = wp::add(var_96, var_90);
        var_98 = dense_index_0(var_1, var_94, var_97);
        var_99 = wp::add(var_M_start, var_98);
        wp::array_store(var_M, var_99, var_91);
        var_101 = wp::extract(var_6, var_69, var_100);
        var_103 = wp::mul(var_3, var_102);
        var_104 = wp::add(var_103, var_69);
        var_106 = wp::mul(var_3, var_105);
        var_107 = wp::add(var_106, var_100);
        var_108 = dense_index_0(var_1, var_104, var_107);
        var_109 = wp::add(var_M_start, var_108);
        wp::array_store(var_M, var_109, var_101);
        var_111 = wp::extract(var_6, var_69, var_110);
        var_113 = wp::mul(var_3, var_112);
        var_114 = wp::add(var_113, var_69);
        var_116 = wp::mul(var_3, var_115);
        var_117 = wp::add(var_116, var_110);
        var_118 = dense_index_0(var_1, var_114, var_117);
        var_119 = wp::add(var_M_start, var_118);
        wp::array_store(var_M, var_119, var_111);
        var_121 = wp::extract(var_6, var_69, var_120);
        var_123 = wp::mul(var_3, var_122);
        var_124 = wp::add(var_123, var_69);
        var_126 = wp::mul(var_3, var_125);
        var_127 = wp::add(var_126, var_120);
        var_128 = dense_index_0(var_1, var_124, var_127);
        var_129 = wp::add(var_M_start, var_128);
        wp::array_store(var_M, var_129, var_121);
        // for j in range(6):                                                                 <L 1558>
        // M[M_start + dense_index(stride, l * 6 + i, l * 6 + j)] = I[i, j]                   <L 1559>
        var_132 = wp::extract(var_6, var_130, var_131);
        var_134 = wp::mul(var_3, var_133);
        var_135 = wp::add(var_134, var_130);
        var_137 = wp::mul(var_3, var_136);
        var_138 = wp::add(var_137, var_131);
        var_139 = dense_index_0(var_1, var_135, var_138);
        var_140 = wp::add(var_M_start, var_139);
        wp::array_store(var_M, var_140, var_132);
        var_142 = wp::extract(var_6, var_130, var_141);
        var_144 = wp::mul(var_3, var_143);
        var_145 = wp::add(var_144, var_130);
        var_147 = wp::mul(var_3, var_146);
        var_148 = wp::add(var_147, var_141);
        var_149 = dense_index_0(var_1, var_145, var_148);
        var_150 = wp::add(var_M_start, var_149);
        wp::array_store(var_M, var_150, var_142);
        var_152 = wp::extract(var_6, var_130, var_151);
        var_154 = wp::mul(var_3, var_153);
        var_155 = wp::add(var_154, var_130);
        var_157 = wp::mul(var_3, var_156);
        var_158 = wp::add(var_157, var_151);
        var_159 = dense_index_0(var_1, var_155, var_158);
        var_160 = wp::add(var_M_start, var_159);
        wp::array_store(var_M, var_160, var_152);
        var_162 = wp::extract(var_6, var_130, var_161);
        var_164 = wp::mul(var_3, var_163);
        var_165 = wp::add(var_164, var_130);
        var_167 = wp::mul(var_3, var_166);
        var_168 = wp::add(var_167, var_161);
        var_169 = dense_index_0(var_1, var_165, var_168);
        var_170 = wp::add(var_M_start, var_169);
        wp::array_store(var_M, var_170, var_162);
        var_172 = wp::extract(var_6, var_130, var_171);
        var_174 = wp::mul(var_3, var_173);
        var_175 = wp::add(var_174, var_130);
        var_177 = wp::mul(var_3, var_176);
        var_178 = wp::add(var_177, var_171);
        var_179 = dense_index_0(var_1, var_175, var_178);
        var_180 = wp::add(var_M_start, var_179);
        wp::array_store(var_M, var_180, var_172);
        var_182 = wp::extract(var_6, var_130, var_181);
        var_184 = wp::mul(var_3, var_183);
        var_185 = wp::add(var_184, var_130);
        var_187 = wp::mul(var_3, var_186);
        var_188 = wp::add(var_187, var_181);
        var_189 = dense_index_0(var_1, var_185, var_188);
        var_190 = wp::add(var_M_start, var_189);
        wp::array_store(var_M, var_190, var_182);
        // for j in range(6):                                                                 <L 1558>
        // M[M_start + dense_index(stride, l * 6 + i, l * 6 + j)] = I[i, j]                   <L 1559>
        var_193 = wp::extract(var_6, var_191, var_192);
        var_195 = wp::mul(var_3, var_194);
        var_196 = wp::add(var_195, var_191);
        var_198 = wp::mul(var_3, var_197);
        var_199 = wp::add(var_198, var_192);
        var_200 = dense_index_0(var_1, var_196, var_199);
        var_201 = wp::add(var_M_start, var_200);
        wp::array_store(var_M, var_201, var_193);
        var_203 = wp::extract(var_6, var_191, var_202);
        var_205 = wp::mul(var_3, var_204);
        var_206 = wp::add(var_205, var_191);
        var_208 = wp::mul(var_3, var_207);
        var_209 = wp::add(var_208, var_202);
        var_210 = dense_index_0(var_1, var_206, var_209);
        var_211 = wp::add(var_M_start, var_210);
        wp::array_store(var_M, var_211, var_203);
        var_213 = wp::extract(var_6, var_191, var_212);
        var_215 = wp::mul(var_3, var_214);
        var_216 = wp::add(var_215, var_191);
        var_218 = wp::mul(var_3, var_217);
        var_219 = wp::add(var_218, var_212);
        var_220 = dense_index_0(var_1, var_216, var_219);
        var_221 = wp::add(var_M_start, var_220);
        wp::array_store(var_M, var_221, var_213);
        var_223 = wp::extract(var_6, var_191, var_222);
        var_225 = wp::mul(var_3, var_224);
        var_226 = wp::add(var_225, var_191);
        var_228 = wp::mul(var_3, var_227);
        var_229 = wp::add(var_228, var_222);
        var_230 = dense_index_0(var_1, var_226, var_229);
        var_231 = wp::add(var_M_start, var_230);
        wp::array_store(var_M, var_231, var_223);
        var_233 = wp::extract(var_6, var_191, var_232);
        var_235 = wp::mul(var_3, var_234);
        var_236 = wp::add(var_235, var_191);
        var_238 = wp::mul(var_3, var_237);
        var_239 = wp::add(var_238, var_232);
        var_240 = dense_index_0(var_1, var_236, var_239);
        var_241 = wp::add(var_M_start, var_240);
        wp::array_store(var_M, var_241, var_233);
        var_243 = wp::extract(var_6, var_191, var_242);
        var_245 = wp::mul(var_3, var_244);
        var_246 = wp::add(var_245, var_191);
        var_248 = wp::mul(var_3, var_247);
        var_249 = wp::add(var_248, var_242);
        var_250 = dense_index_0(var_1, var_246, var_249);
        var_251 = wp::add(var_M_start, var_250);
        wp::array_store(var_M, var_251, var_243);
        // for j in range(6):                                                                 <L 1558>
        // M[M_start + dense_index(stride, l * 6 + i, l * 6 + j)] = I[i, j]                   <L 1559>
        var_254 = wp::extract(var_6, var_252, var_253);
        var_256 = wp::mul(var_3, var_255);
        var_257 = wp::add(var_256, var_252);
        var_259 = wp::mul(var_3, var_258);
        var_260 = wp::add(var_259, var_253);
        var_261 = dense_index_0(var_1, var_257, var_260);
        var_262 = wp::add(var_M_start, var_261);
        wp::array_store(var_M, var_262, var_254);
        var_264 = wp::extract(var_6, var_252, var_263);
        var_266 = wp::mul(var_3, var_265);
        var_267 = wp::add(var_266, var_252);
        var_269 = wp::mul(var_3, var_268);
        var_270 = wp::add(var_269, var_263);
        var_271 = dense_index_0(var_1, var_267, var_270);
        var_272 = wp::add(var_M_start, var_271);
        wp::array_store(var_M, var_272, var_264);
        var_274 = wp::extract(var_6, var_252, var_273);
        var_276 = wp::mul(var_3, var_275);
        var_277 = wp::add(var_276, var_252);
        var_279 = wp::mul(var_3, var_278);
        var_280 = wp::add(var_279, var_273);
        var_281 = dense_index_0(var_1, var_277, var_280);
        var_282 = wp::add(var_M_start, var_281);
        wp::array_store(var_M, var_282, var_274);
        var_284 = wp::extract(var_6, var_252, var_283);
        var_286 = wp::mul(var_3, var_285);
        var_287 = wp::add(var_286, var_252);
        var_289 = wp::mul(var_3, var_288);
        var_290 = wp::add(var_289, var_283);
        var_291 = dense_index_0(var_1, var_287, var_290);
        var_292 = wp::add(var_M_start, var_291);
        wp::array_store(var_M, var_292, var_284);
        var_294 = wp::extract(var_6, var_252, var_293);
        var_296 = wp::mul(var_3, var_295);
        var_297 = wp::add(var_296, var_252);
        var_299 = wp::mul(var_3, var_298);
        var_300 = wp::add(var_299, var_293);
        var_301 = dense_index_0(var_1, var_297, var_300);
        var_302 = wp::add(var_M_start, var_301);
        wp::array_store(var_M, var_302, var_294);
        var_304 = wp::extract(var_6, var_252, var_303);
        var_306 = wp::mul(var_3, var_305);
        var_307 = wp::add(var_306, var_252);
        var_309 = wp::mul(var_3, var_308);
        var_310 = wp::add(var_309, var_303);
        var_311 = dense_index_0(var_1, var_307, var_310);
        var_312 = wp::add(var_M_start, var_311);
        wp::array_store(var_M, var_312, var_304);
        // for j in range(6):                                                                 <L 1558>
        // M[M_start + dense_index(stride, l * 6 + i, l * 6 + j)] = I[i, j]                   <L 1559>
        var_315 = wp::extract(var_6, var_313, var_314);
        var_317 = wp::mul(var_3, var_316);
        var_318 = wp::add(var_317, var_313);
        var_320 = wp::mul(var_3, var_319);
        var_321 = wp::add(var_320, var_314);
        var_322 = dense_index_0(var_1, var_318, var_321);
        var_323 = wp::add(var_M_start, var_322);
        wp::array_store(var_M, var_323, var_315);
        var_325 = wp::extract(var_6, var_313, var_324);
        var_327 = wp::mul(var_3, var_326);
        var_328 = wp::add(var_327, var_313);
        var_330 = wp::mul(var_3, var_329);
        var_331 = wp::add(var_330, var_324);
        var_332 = dense_index_0(var_1, var_328, var_331);
        var_333 = wp::add(var_M_start, var_332);
        wp::array_store(var_M, var_333, var_325);
        var_335 = wp::extract(var_6, var_313, var_334);
        var_337 = wp::mul(var_3, var_336);
        var_338 = wp::add(var_337, var_313);
        var_340 = wp::mul(var_3, var_339);
        var_341 = wp::add(var_340, var_334);
        var_342 = dense_index_0(var_1, var_338, var_341);
        var_343 = wp::add(var_M_start, var_342);
        wp::array_store(var_M, var_343, var_335);
        var_345 = wp::extract(var_6, var_313, var_344);
        var_347 = wp::mul(var_3, var_346);
        var_348 = wp::add(var_347, var_313);
        var_350 = wp::mul(var_3, var_349);
        var_351 = wp::add(var_350, var_344);
        var_352 = dense_index_0(var_1, var_348, var_351);
        var_353 = wp::add(var_M_start, var_352);
        wp::array_store(var_M, var_353, var_345);
        var_355 = wp::extract(var_6, var_313, var_354);
        var_357 = wp::mul(var_3, var_356);
        var_358 = wp::add(var_357, var_313);
        var_360 = wp::mul(var_3, var_359);
        var_361 = wp::add(var_360, var_354);
        var_362 = dense_index_0(var_1, var_358, var_361);
        var_363 = wp::add(var_M_start, var_362);
        wp::array_store(var_M, var_363, var_355);
        var_365 = wp::extract(var_6, var_313, var_364);
        var_367 = wp::mul(var_3, var_366);
        var_368 = wp::add(var_367, var_313);
        var_370 = wp::mul(var_3, var_369);
        var_371 = wp::add(var_370, var_364);
        var_372 = dense_index_0(var_1, var_368, var_371);
        var_373 = wp::add(var_M_start, var_372);
        wp::array_store(var_M, var_373, var_365);
        goto start_for_0;
    end_for_0:;
}


// newton/_src/solvers/featherstone/kernels.py:1583
static void dense_gemm_0(
    wp::int32 var_m,
    wp::int32 var_n,
    wp::int32 var_p,
    bool var_transpose_A,
    bool var_transpose_B,
    wp::int32 var_A_start,
    wp::int32 var_B_start,
    wp::int32 var_C_start,
    wp::array_t<wp::float32> var_A,
    wp::array_t<wp::float32> var_B,
    wp::array_t<wp::float32> var_C)
{
    //---------
    // primal vars
    wp::range_t var_0{};
    wp::int32 var_1{};
    wp::range_t var_2{};
    wp::int32 var_3{};
    const wp::float32 var_4 = 0.0;
    wp::float32 var_5{};
    wp::range_t var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    wp::int32 var_13{};
    wp::int32 var_14{};
    wp::int32 var_15{};
    wp::int32 var_16{};
    wp::int32 var_17{};
    wp::int32 var_18{};
    wp::float32* var_19{};
    wp::int32 var_20{};
    wp::float32* var_21{};
    wp::float32 var_22{};
    wp::float32 var_23{};
    wp::float32 var_24{};
    wp::float32 var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    wp::int32 var_28{};
    //---------
    // forward
    // def dense_gemm(                                                                        <L 1584>
    // for i in range(m):                                                                     <L 1599>
    var_0 = wp::range(var_m);
    start_for_0:;
        if (iter_cmp(var_0) == 0) goto end_for_0;
        var_1 = wp::iter_next(var_0);
        // for j in range(n):                                                                 <L 1600>
        var_2 = wp::range(var_n);
        start_for_2:;
            if (iter_cmp(var_2) == 0) goto end_for_2;
            var_3 = wp::iter_next(var_2);
            // sum = float(0.0)                                                               <L 1601>
            var_5 = wp::float(var_4);
            // for k in range(p):                                                             <L 1602>
            var_6 = wp::range(var_p);
            start_for_4:;
                if (iter_cmp(var_6) == 0) goto end_for_4;
                var_7 = wp::iter_next(var_6);
                // if transpose_A:                                                            <L 1603>
                if (var_transpose_A) {
                    // a_i = k * m + i                                                        <L 1604>
                    var_8 = wp::mul(var_7, var_m);
                    var_9 = wp::add(var_8, var_1);
                }
                if (!var_transpose_A) {
                    // a_i = i * p + k                                                        <L 1606>
                    var_10 = wp::mul(var_1, var_p);
                    var_11 = wp::add(var_10, var_7);
                }
                var_12 = wp::where(var_transpose_A, var_9, var_11);
                // if transpose_B:                                                            <L 1607>
                if (var_transpose_B) {
                    // b_j = j * p + k                                                        <L 1608>
                    var_13 = wp::mul(var_3, var_p);
                    var_14 = wp::add(var_13, var_7);
                }
                if (!var_transpose_B) {
                    // b_j = k * n + j                                                        <L 1610>
                    var_15 = wp::mul(var_7, var_n);
                    var_16 = wp::add(var_15, var_3);
                }
                var_17 = wp::where(var_transpose_B, var_14, var_16);
                // sum += A[A_start + a_i] * B[B_start + b_j]                                 <L 1611>
                var_18 = wp::add(var_A_start, var_12);
                var_19 = wp::address(var_A, var_18);
                var_20 = wp::add(var_B_start, var_17);
                var_21 = wp::address(var_B, var_20);
                var_23 = wp::load(var_19);
                var_24 = wp::load(var_21);
                var_22 = wp::mul(var_23, var_24);
                var_25 = wp::add(var_5, var_22);
                wp::assign(var_5, var_25);
                goto start_for_4;
            end_for_4:;
            // C[C_start + i * n + j] = sum                                                   <L 1613>
            var_26 = wp::mul(var_1, var_n);
            var_27 = wp::add(var_C_start, var_26);
            var_28 = wp::add(var_27, var_3);
            wp::array_store(var_C, var_28, var_5);
            goto start_for_2;
        end_for_2:;
        goto start_for_0;
    end_for_0:;
}


// newton/_src/solvers/featherstone/kernels.py:1699
static void dense_cholesky_0(
    wp::int32 var_n,
    wp::array_t<wp::float32> var_A,
    wp::array_t<wp::float32> var_R,
    wp::int32 var_A_start,
    wp::int32 var_R_start,
    wp::array_t<wp::float32> var_L)
{
    //---------
    // primal vars
    wp::range_t var_0{};
    wp::int32 var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::float32* var_4{};
    wp::int32 var_5{};
    wp::float32* var_6{};
    wp::float32 var_7{};
    wp::float32 var_8{};
    wp::float32 var_9{};
    wp::range_t var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    wp::int32 var_13{};
    wp::float32* var_14{};
    wp::float32 var_15{};
    wp::float32 var_16{};
    wp::float32 var_17{};
    wp::float32 var_18{};
    wp::float32 var_19{};
    const wp::float32 var_20 = 1.0;
    wp::float32 var_21{};
    wp::int32 var_22{};
    wp::int32 var_23{};
    const wp::int32 var_24 = 1;
    wp::int32 var_25{};
    wp::range_t var_26{};
    wp::int32 var_27{};
    wp::int32 var_28{};
    wp::int32 var_29{};
    wp::float32* var_30{};
    wp::float32 var_31{};
    wp::float32 var_32{};
    wp::range_t var_33{};
    wp::int32 var_34{};
    wp::int32 var_35{};
    wp::int32 var_36{};
    wp::float32* var_37{};
    wp::int32 var_38{};
    wp::int32 var_39{};
    wp::float32* var_40{};
    wp::float32 var_41{};
    wp::float32 var_42{};
    wp::float32 var_43{};
    wp::float32 var_44{};
    wp::float32 var_45{};
    wp::int32 var_46{};
    wp::int32 var_47{};
    //---------
    // forward
    // def dense_cholesky(                                                                    <L 1700>
    // for j in range(n):                                                                     <L 1710>
    var_0 = wp::range(var_n);
    start_for_0:;
        if (iter_cmp(var_0) == 0) goto end_for_0;
        var_1 = wp::iter_next(var_0);
        // s = A[A_start + dense_index(n, j, j)] + R[R_start + j]                             <L 1711>
        var_2 = dense_index_0(var_n, var_1, var_1);
        var_3 = wp::add(var_A_start, var_2);
        var_4 = wp::address(var_A, var_3);
        var_5 = wp::add(var_R_start, var_1);
        var_6 = wp::address(var_R, var_5);
        var_8 = wp::load(var_4);
        var_9 = wp::load(var_6);
        var_7 = wp::add(var_8, var_9);
        // for k in range(j):                                                                 <L 1713>
        var_10 = wp::range(var_1);
        start_for_2:;
            if (iter_cmp(var_10) == 0) goto end_for_2;
            var_11 = wp::iter_next(var_10);
            // r = L[A_start + dense_index(n, j, k)]                                          <L 1714>
            var_12 = dense_index_0(var_n, var_1, var_11);
            var_13 = wp::add(var_A_start, var_12);
            var_14 = wp::address(var_L, var_13);
            var_16 = wp::load(var_14);
            var_15 = wp::copy(var_16);
            // s -= r * r                                                                     <L 1715>
            var_17 = wp::mul(var_15, var_15);
            var_18 = wp::sub(var_7, var_17);
            wp::assign(var_7, var_18);
            goto start_for_2;
        end_for_2:;
        // s = wp.sqrt(s)                                                                     <L 1717>
        var_19 = wp::sqrt(var_7);
        // invS = 1.0 / s                                                                     <L 1718>
        var_21 = wp::div(var_20, var_19);
        // L[A_start + dense_index(n, j, j)] = s                                              <L 1720>
        var_22 = dense_index_0(var_n, var_1, var_1);
        var_23 = wp::add(var_A_start, var_22);
        wp::array_store(var_L, var_23, var_19);
        // for i in range(j + 1, n):                                                          <L 1722>
        var_25 = wp::add(var_1, var_24);
        var_26 = wp::range(var_25, var_n);
        start_for_4:;
            if (iter_cmp(var_26) == 0) goto end_for_4;
            var_27 = wp::iter_next(var_26);
            // s = A[A_start + dense_index(n, i, j)]                                          <L 1723>
            var_28 = dense_index_0(var_n, var_27, var_1);
            var_29 = wp::add(var_A_start, var_28);
            var_30 = wp::address(var_A, var_29);
            var_32 = wp::load(var_30);
            var_31 = wp::copy(var_32);
            // for k in range(j):                                                             <L 1725>
            var_33 = wp::range(var_1);
            start_for_6:;
                if (iter_cmp(var_33) == 0) goto end_for_6;
                var_34 = wp::iter_next(var_33);
                // s -= L[A_start + dense_index(n, i, k)] * L[A_start + dense_index(n, j, k)]       <L 1726>
                var_35 = dense_index_0(var_n, var_27, var_34);
                var_36 = wp::add(var_A_start, var_35);
                var_37 = wp::address(var_L, var_36);
                var_38 = dense_index_0(var_n, var_1, var_34);
                var_39 = wp::add(var_A_start, var_38);
                var_40 = wp::address(var_L, var_39);
                var_42 = wp::load(var_37);
                var_43 = wp::load(var_40);
                var_41 = wp::mul(var_42, var_43);
                var_44 = wp::sub(var_31, var_41);
                wp::assign(var_31, var_44);
                goto start_for_6;
            end_for_6:;
            // L[A_start + dense_index(n, i, j)] = s * invS                                   <L 1728>
            var_45 = wp::mul(var_31, var_21);
            var_46 = dense_index_0(var_n, var_27, var_1);
            var_47 = wp::add(var_A_start, var_46);
            wp::array_store(var_L, var_47, var_45);
            wp::assign(var_19, var_31);
            wp::assign(var_11, var_34);
            goto start_for_4;
        end_for_4:;
        goto start_for_0;
    end_for_0:;
}


// newton/_src/solvers/featherstone/kernels.py:1905
static wp::transform_t<wp::float32> integrate_body_pose_from_com_twist_0(
    wp::transform_t<wp::float32> var_X_wb,
    wp::vec_t<3, wp::float32> var_body_com,
    wp::vec_t<6, wp::float32> var_qd_com_world,
    wp::float32 var_dt)
{
    //---------
    // primal vars
    wp::quat_t<wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    const wp::float32 var_4 = 0.0;
    wp::quat_t<wp::float32> var_5{};
    wp::quat_t<wp::float32> var_6{};
    const wp::float32 var_7 = 0.5;
    wp::quat_t<wp::float32> var_8{};
    wp::quat_t<wp::float32> var_9{};
    wp::quat_t<wp::float32> var_10{};
    wp::quat_t<wp::float32> var_11{};
    wp::vec_t<3, wp::float32> var_12{};
    wp::vec_t<3, wp::float32> var_13{};
    wp::vec_t<3, wp::float32> var_14{};
    wp::vec_t<3, wp::float32> var_15{};
    wp::transform_t<wp::float32> var_16{};
    //---------
    // forward
    // def integrate_body_pose_from_com_twist(                                                <L 1906>
    // q = wp.transform_get_rotation(X_wb)                                                    <L 1912>
    var_0 = wp::transform_get_rotation(var_X_wb);
    // x_com = wp.transform_point(X_wb, body_com)                                             <L 1913>
    var_1 = wp::transform_point(var_X_wb, var_body_com);
    // v_com = wp.spatial_top(qd_com_world)                                                   <L 1915>
    var_2 = wp::spatial_top(var_qd_com_world);
    // w = wp.spatial_bottom(qd_com_world)                                                    <L 1916>
    var_3 = wp::spatial_bottom(var_qd_com_world);
    // drdt = wp.quat(w, 0.0) * q * 0.5                                                       <L 1918>
    var_5 = wp::quat_t<wp::float32>(var_3, var_4);
    var_6 = wp::mul(var_5, var_0);
    var_8 = wp::mul(var_6, var_7);
    // q_new = wp.normalize(q + drdt * dt)                                                    <L 1919>
    var_9 = wp::mul(var_8, var_dt);
    var_10 = wp::add(var_0, var_9);
    var_11 = wp::normalize(var_10);
    // x_com_new = x_com + v_com * dt                                                         <L 1920>
    var_12 = wp::mul(var_2, var_dt);
    var_13 = wp::add(var_1, var_12);
    // x_origin_new = x_com_new - wp.quat_rotate(q_new, body_com)                             <L 1921>
    var_14 = wp::quat_rotate(var_11, var_body_com);
    var_15 = wp::sub(var_13, var_14);
    // return wp.transform(x_origin_new, q_new)                                               <L 1923>
    var_16 = wp::transform_t<wp::float32>(var_15, var_11);
    return var_16;
}


// newton/_src/solvers/semi_implicit/kernels_body.py:18
static wp::float32 joint_force_0(
    wp::float32 var_q,
    wp::float32 var_qd,
    wp::float32 var_joint_target_q,
    wp::float32 var_joint_target_qd,
    wp::float32 var_target_ke,
    wp::float32 var_target_kd,
    wp::float32 var_limit_lower,
    wp::float32 var_limit_upper,
    wp::float32 var_limit_ke,
    wp::float32 var_limit_kd,
    wp::float32 var_damping)
{
    //---------
    // primal vars
    const wp::float32 var_0 = 0.0;
    const wp::float32 var_1 = 0.0;
    const wp::float32 var_2 = 0.0;
    wp::float32 var_3{};
    wp::float32 var_4{};
    wp::float32 var_5{};
    wp::float32 var_6{};
    wp::float32 var_7{};
    bool var_8{};
    wp::float32 var_9{};
    wp::float32 var_10{};
    wp::float32 var_11{};
    wp::float32 var_12{};
    const wp::float32 var_13 = 0.0;
    bool var_14{};
    wp::float32 var_15{};
    wp::float32 var_16{};
    wp::float32 var_17{};
    wp::float32 var_18{};
    const wp::float32 var_19 = 0.0;
    wp::float32 var_20{};
    wp::float32 var_21{};
    wp::float32 var_22{};
    wp::float32 var_23{};
    wp::float32 var_24{};
    wp::float32 var_25{};
    wp::float32 var_26{};
    wp::float32 var_27{};
    wp::float32 var_28{};
    wp::float32 var_29{};
    wp::float32 var_30{};
    //---------
    // forward
    // def joint_force(                                                                       <L 19>
    // limit_f = 0.0                                                                          <L 34>
    // damping_f = 0.0                                                                        <L 35>
    // target_f = 0.0                                                                         <L 36>
    // target_f = target_ke * (joint_target_q - q) + target_kd * (joint_target_qd - qd)       <L 38>
    var_3 = wp::sub(var_joint_target_q, var_q);
    var_4 = wp::mul(var_target_ke, var_3);
    var_5 = wp::sub(var_joint_target_qd, var_qd);
    var_6 = wp::mul(var_target_kd, var_5);
    var_7 = wp::add(var_4, var_6);
    // if q < limit_lower:                                                                    <L 41>
    var_8 = (var_q < var_limit_lower);
    if (var_8) {
        // limit_f = limit_ke * (limit_lower - q)                                             <L 42>
        var_9 = wp::sub(var_limit_lower, var_q);
        var_10 = wp::mul(var_limit_ke, var_9);
        // damping_f = -limit_kd * qd                                                         <L 43>
        var_11 = wp::neg(var_limit_kd);
        var_12 = wp::mul(var_11, var_qd);
        // target_f = 0.0                                                                     <L 44>
    }
    if (!var_8) {
        // elif q > limit_upper:                                                              <L 45>
        var_14 = (var_q > var_limit_upper);
        if (var_14) {
            // limit_f = limit_ke * (limit_upper - q)                                         <L 46>
            var_15 = wp::sub(var_limit_upper, var_q);
            var_16 = wp::mul(var_limit_ke, var_15);
            // damping_f = -limit_kd * qd                                                     <L 47>
            var_17 = wp::neg(var_limit_kd);
            var_18 = wp::mul(var_17, var_qd);
            // target_f = 0.0                                                                 <L 48>
        }
        var_20 = wp::where(var_14, var_16, var_0);
        var_21 = wp::where(var_14, var_18, var_1);
        var_22 = wp::where(var_14, var_19, var_7);
    }
    var_23 = wp::where(var_8, var_10, var_20);
    var_24 = wp::where(var_8, var_12, var_21);
    var_25 = wp::where(var_8, var_13, var_22);
    // passive_f = -damping * qd                                                              <L 50>
    var_26 = wp::neg(var_damping);
    var_27 = wp::mul(var_26, var_qd);
    // return limit_f + damping_f + target_f + passive_f                                      <L 52>
    var_28 = wp::add(var_23, var_24);
    var_29 = wp::add(var_28, var_25);
    var_30 = wp::add(var_29, var_27);
    return var_30;
}


// newton/_src/solvers/featherstone/kernels.py:503
static void jcalc_tau_0(
    wp::int32 var_type,
    wp::array_t<wp::float32> var_joint_target_ke,
    wp::array_t<wp::float32> var_joint_target_kd,
    wp::array_t<wp::float32> var_joint_limit_ke,
    wp::array_t<wp::float32> var_joint_limit_kd,
    wp::array_t<wp::float32> var_joint_damping,
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::float32> var_joint_f,
    wp::array_t<wp::float32> var_joint_target_q,
    wp::array_t<wp::float32> var_joint_target_qd,
    wp::array_t<wp::float32> var_joint_limit_lower,
    wp::array_t<wp::float32> var_joint_limit_upper,
    wp::int32 var_coord_start,
    wp::int32 var_dof_start,
    wp::int32 var_target_q_start,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::vec_t<6, wp::float32> var_body_f_s,
    wp::array_t<wp::float32> var_tau)
{
    //---------
    // primal vars
    const wp::int32 var_0 = 2;
    bool var_1{};
    const wp::int32 var_2 = 0;
    wp::int32 var_3{};
    wp::vec_t<6, wp::float32>* var_4{};
    wp::vec_t<6, wp::float32> var_5{};
    wp::vec_t<6, wp::float32> var_6{};
    wp::int32 var_7{};
    wp::float32* var_8{};
    wp::float32 var_9{};
    wp::float32 var_10{};
    wp::float32* var_11{};
    wp::float32 var_12{};
    wp::float32 var_13{};
    wp::float32 var_14{};
    wp::float32 var_15{};
    wp::float32* var_16{};
    wp::float32 var_17{};
    wp::float32 var_18{};
    wp::float32 var_19{};
    const wp::int32 var_20 = 1;
    wp::int32 var_21{};
    wp::vec_t<6, wp::float32>* var_22{};
    wp::vec_t<6, wp::float32> var_23{};
    wp::vec_t<6, wp::float32> var_24{};
    wp::int32 var_25{};
    wp::float32* var_26{};
    wp::float32 var_27{};
    wp::float32 var_28{};
    wp::float32* var_29{};
    wp::float32 var_30{};
    wp::float32 var_31{};
    wp::float32 var_32{};
    wp::float32 var_33{};
    wp::float32* var_34{};
    wp::float32 var_35{};
    wp::float32 var_36{};
    wp::float32 var_37{};
    const wp::int32 var_38 = 2;
    wp::int32 var_39{};
    wp::vec_t<6, wp::float32>* var_40{};
    wp::vec_t<6, wp::float32> var_41{};
    wp::vec_t<6, wp::float32> var_42{};
    wp::int32 var_43{};
    wp::float32* var_44{};
    wp::float32 var_45{};
    wp::float32 var_46{};
    wp::float32* var_47{};
    wp::float32 var_48{};
    wp::float32 var_49{};
    wp::float32 var_50{};
    wp::float32 var_51{};
    wp::float32* var_52{};
    wp::float32 var_53{};
    wp::float32 var_54{};
    wp::float32 var_55{};
    bool var_56{};
    const wp::int32 var_57 = 4;
    bool var_58{};
    const wp::int32 var_59 = 5;
    bool var_60{};
    const wp::int32 var_61 = 0;
    wp::int32 var_62{};
    wp::vec_t<6, wp::float32>* var_63{};
    wp::vec_t<6, wp::float32> var_64{};
    wp::vec_t<6, wp::float32> var_65{};
    wp::float32 var_66{};
    wp::float32 var_67{};
    wp::int32 var_68{};
    wp::float32* var_69{};
    wp::float32 var_70{};
    wp::float32 var_71{};
    wp::int32 var_72{};
    const wp::int32 var_73 = 1;
    wp::int32 var_74{};
    wp::vec_t<6, wp::float32>* var_75{};
    wp::vec_t<6, wp::float32> var_76{};
    wp::vec_t<6, wp::float32> var_77{};
    wp::float32 var_78{};
    wp::float32 var_79{};
    wp::int32 var_80{};
    wp::float32* var_81{};
    wp::float32 var_82{};
    wp::float32 var_83{};
    wp::int32 var_84{};
    const wp::int32 var_85 = 2;
    wp::int32 var_86{};
    wp::vec_t<6, wp::float32>* var_87{};
    wp::vec_t<6, wp::float32> var_88{};
    wp::vec_t<6, wp::float32> var_89{};
    wp::float32 var_90{};
    wp::float32 var_91{};
    wp::int32 var_92{};
    wp::float32* var_93{};
    wp::float32 var_94{};
    wp::float32 var_95{};
    wp::int32 var_96{};
    const wp::int32 var_97 = 3;
    wp::int32 var_98{};
    wp::vec_t<6, wp::float32>* var_99{};
    wp::vec_t<6, wp::float32> var_100{};
    wp::vec_t<6, wp::float32> var_101{};
    wp::float32 var_102{};
    wp::float32 var_103{};
    wp::int32 var_104{};
    wp::float32* var_105{};
    wp::float32 var_106{};
    wp::float32 var_107{};
    wp::int32 var_108{};
    const wp::int32 var_109 = 4;
    wp::int32 var_110{};
    wp::vec_t<6, wp::float32>* var_111{};
    wp::vec_t<6, wp::float32> var_112{};
    wp::vec_t<6, wp::float32> var_113{};
    wp::float32 var_114{};
    wp::float32 var_115{};
    wp::int32 var_116{};
    wp::float32* var_117{};
    wp::float32 var_118{};
    wp::float32 var_119{};
    wp::int32 var_120{};
    const wp::int32 var_121 = 5;
    wp::int32 var_122{};
    wp::vec_t<6, wp::float32>* var_123{};
    wp::vec_t<6, wp::float32> var_124{};
    wp::vec_t<6, wp::float32> var_125{};
    wp::float32 var_126{};
    wp::float32 var_127{};
    wp::int32 var_128{};
    wp::float32* var_129{};
    wp::float32 var_130{};
    wp::float32 var_131{};
    wp::int32 var_132{};
    wp::int32 var_133{};
    wp::vec_t<6, wp::float32> var_134{};
    bool var_135{};
    const wp::int32 var_136 = 0;
    bool var_137{};
    const wp::int32 var_138 = 1;
    bool var_139{};
    const wp::int32 var_140 = 6;
    bool var_141{};
    wp::int32 var_142{};
    wp::range_t var_143{};
    wp::int32 var_144{};
    wp::int32 var_145{};
    wp::vec_t<6, wp::float32>* var_146{};
    wp::vec_t<6, wp::float32> var_147{};
    wp::vec_t<6, wp::float32> var_148{};
    wp::int32 var_149{};
    wp::float32* var_150{};
    wp::float32 var_151{};
    wp::float32 var_152{};
    wp::float32* var_153{};
    wp::float32 var_154{};
    wp::float32 var_155{};
    wp::float32* var_156{};
    wp::float32 var_157{};
    wp::float32 var_158{};
    wp::float32* var_159{};
    wp::float32 var_160{};
    wp::float32 var_161{};
    wp::float32* var_162{};
    wp::float32 var_163{};
    wp::float32 var_164{};
    wp::float32* var_165{};
    wp::float32 var_166{};
    wp::float32 var_167{};
    wp::float32* var_168{};
    wp::float32 var_169{};
    wp::float32 var_170{};
    wp::float32* var_171{};
    wp::float32 var_172{};
    wp::float32 var_173{};
    wp::int32 var_174{};
    wp::float32* var_175{};
    wp::float32 var_176{};
    wp::float32 var_177{};
    wp::float32* var_178{};
    wp::float32 var_179{};
    wp::float32 var_180{};
    wp::float32* var_181{};
    wp::float32 var_182{};
    wp::float32 var_183{};
    wp::float32 var_184{};
    wp::float32 var_185{};
    wp::float32 var_186{};
    wp::float32 var_187{};
    wp::float32* var_188{};
    wp::float32 var_189{};
    wp::float32 var_190{};
    wp::int32 var_191{};
    //---------
    // forward
    // def jcalc_tau(                                                                         <L 504>
    // if type == JointType.BALL:                                                             <L 528>
    var_1 = (var_type == var_0);
    if (var_1) {
        // for i in range(3):                                                                 <L 532>
        // S_s = joint_S_s[dof_start + i]                                                     <L 533>
        var_3 = wp::add(var_dof_start, var_2);
        var_4 = wp::address(var_joint_S_s, var_3);
        var_6 = wp::load(var_4);
        var_5 = wp::copy(var_6);
        // j = dof_start + i                                                                  <L 538>
        var_7 = wp::add(var_dof_start, var_2);
        // passive_f = -joint_damping[j] * joint_qd[j]                                        <L 539>
        var_8 = wp::address(var_joint_damping, var_7);
        var_10 = wp::load(var_8);
        var_9 = wp::neg(var_10);
        var_11 = wp::address(var_joint_qd, var_7);
        var_13 = wp::load(var_11);
        var_12 = wp::mul(var_9, var_13);
        // tau[j] = -wp.dot(S_s, body_f_s) + joint_f[j] + passive_f                           <L 540>
        var_14 = wp::dot(var_5, var_body_f_s);
        var_15 = wp::neg(var_14);
        var_16 = wp::address(var_joint_f, var_7);
        var_18 = wp::load(var_16);
        var_17 = wp::add(var_15, var_18);
        var_19 = wp::add(var_17, var_12);
        wp::array_store(var_tau, var_7, var_19);
        // S_s = joint_S_s[dof_start + i]                                                     <L 533>
        var_21 = wp::add(var_dof_start, var_20);
        var_22 = wp::address(var_joint_S_s, var_21);
        var_24 = wp::load(var_22);
        var_23 = wp::copy(var_24);
        // j = dof_start + i                                                                  <L 538>
        var_25 = wp::add(var_dof_start, var_20);
        // passive_f = -joint_damping[j] * joint_qd[j]                                        <L 539>
        var_26 = wp::address(var_joint_damping, var_25);
        var_28 = wp::load(var_26);
        var_27 = wp::neg(var_28);
        var_29 = wp::address(var_joint_qd, var_25);
        var_31 = wp::load(var_29);
        var_30 = wp::mul(var_27, var_31);
        // tau[j] = -wp.dot(S_s, body_f_s) + joint_f[j] + passive_f                           <L 540>
        var_32 = wp::dot(var_23, var_body_f_s);
        var_33 = wp::neg(var_32);
        var_34 = wp::address(var_joint_f, var_25);
        var_36 = wp::load(var_34);
        var_35 = wp::add(var_33, var_36);
        var_37 = wp::add(var_35, var_30);
        wp::array_store(var_tau, var_25, var_37);
        // S_s = joint_S_s[dof_start + i]                                                     <L 533>
        var_39 = wp::add(var_dof_start, var_38);
        var_40 = wp::address(var_joint_S_s, var_39);
        var_42 = wp::load(var_40);
        var_41 = wp::copy(var_42);
        // j = dof_start + i                                                                  <L 538>
        var_43 = wp::add(var_dof_start, var_38);
        // passive_f = -joint_damping[j] * joint_qd[j]                                        <L 539>
        var_44 = wp::address(var_joint_damping, var_43);
        var_46 = wp::load(var_44);
        var_45 = wp::neg(var_46);
        var_47 = wp::address(var_joint_qd, var_43);
        var_49 = wp::load(var_47);
        var_48 = wp::mul(var_45, var_49);
        // tau[j] = -wp.dot(S_s, body_f_s) + joint_f[j] + passive_f                           <L 540>
        var_50 = wp::dot(var_41, var_body_f_s);
        var_51 = wp::neg(var_50);
        var_52 = wp::address(var_joint_f, var_43);
        var_54 = wp::load(var_52);
        var_53 = wp::add(var_51, var_54);
        var_55 = wp::add(var_53, var_48);
        wp::array_store(var_tau, var_43, var_55);
        // return                                                                             <L 543>
        return;
    }
    // if type == JointType.FREE or type == JointType.DISTANCE:                               <L 545>
    var_58 = (var_type == var_57);
    var_56 = var_58;
    if (!var_56) {
        var_60 = (var_type == var_59);
        var_56 = var_56 || var_60;
    }
    if (var_56) {
        // for i in range(6):                                                                 <L 546>
        // S_s = joint_S_s[dof_start + i]                                                     <L 547>
        var_62 = wp::add(var_dof_start, var_61);
        var_63 = wp::address(var_joint_S_s, var_62);
        var_65 = wp::load(var_63);
        var_64 = wp::copy(var_65);
        // tau[dof_start + i] = -wp.dot(S_s, body_f_s) + joint_f[dof_start + i]               <L 548>
        var_66 = wp::dot(var_64, var_body_f_s);
        var_67 = wp::neg(var_66);
        var_68 = wp::add(var_dof_start, var_61);
        var_69 = wp::address(var_joint_f, var_68);
        var_71 = wp::load(var_69);
        var_70 = wp::add(var_67, var_71);
        var_72 = wp::add(var_dof_start, var_61);
        wp::array_store(var_tau, var_72, var_70);
        // S_s = joint_S_s[dof_start + i]                                                     <L 547>
        var_74 = wp::add(var_dof_start, var_73);
        var_75 = wp::address(var_joint_S_s, var_74);
        var_77 = wp::load(var_75);
        var_76 = wp::copy(var_77);
        // tau[dof_start + i] = -wp.dot(S_s, body_f_s) + joint_f[dof_start + i]               <L 548>
        var_78 = wp::dot(var_76, var_body_f_s);
        var_79 = wp::neg(var_78);
        var_80 = wp::add(var_dof_start, var_73);
        var_81 = wp::address(var_joint_f, var_80);
        var_83 = wp::load(var_81);
        var_82 = wp::add(var_79, var_83);
        var_84 = wp::add(var_dof_start, var_73);
        wp::array_store(var_tau, var_84, var_82);
        // S_s = joint_S_s[dof_start + i]                                                     <L 547>
        var_86 = wp::add(var_dof_start, var_85);
        var_87 = wp::address(var_joint_S_s, var_86);
        var_89 = wp::load(var_87);
        var_88 = wp::copy(var_89);
        // tau[dof_start + i] = -wp.dot(S_s, body_f_s) + joint_f[dof_start + i]               <L 548>
        var_90 = wp::dot(var_88, var_body_f_s);
        var_91 = wp::neg(var_90);
        var_92 = wp::add(var_dof_start, var_85);
        var_93 = wp::address(var_joint_f, var_92);
        var_95 = wp::load(var_93);
        var_94 = wp::add(var_91, var_95);
        var_96 = wp::add(var_dof_start, var_85);
        wp::array_store(var_tau, var_96, var_94);
        // S_s = joint_S_s[dof_start + i]                                                     <L 547>
        var_98 = wp::add(var_dof_start, var_97);
        var_99 = wp::address(var_joint_S_s, var_98);
        var_101 = wp::load(var_99);
        var_100 = wp::copy(var_101);
        // tau[dof_start + i] = -wp.dot(S_s, body_f_s) + joint_f[dof_start + i]               <L 548>
        var_102 = wp::dot(var_100, var_body_f_s);
        var_103 = wp::neg(var_102);
        var_104 = wp::add(var_dof_start, var_97);
        var_105 = wp::address(var_joint_f, var_104);
        var_107 = wp::load(var_105);
        var_106 = wp::add(var_103, var_107);
        var_108 = wp::add(var_dof_start, var_97);
        wp::array_store(var_tau, var_108, var_106);
        // S_s = joint_S_s[dof_start + i]                                                     <L 547>
        var_110 = wp::add(var_dof_start, var_109);
        var_111 = wp::address(var_joint_S_s, var_110);
        var_113 = wp::load(var_111);
        var_112 = wp::copy(var_113);
        // tau[dof_start + i] = -wp.dot(S_s, body_f_s) + joint_f[dof_start + i]               <L 548>
        var_114 = wp::dot(var_112, var_body_f_s);
        var_115 = wp::neg(var_114);
        var_116 = wp::add(var_dof_start, var_109);
        var_117 = wp::address(var_joint_f, var_116);
        var_119 = wp::load(var_117);
        var_118 = wp::add(var_115, var_119);
        var_120 = wp::add(var_dof_start, var_109);
        wp::array_store(var_tau, var_120, var_118);
        // S_s = joint_S_s[dof_start + i]                                                     <L 547>
        var_122 = wp::add(var_dof_start, var_121);
        var_123 = wp::address(var_joint_S_s, var_122);
        var_125 = wp::load(var_123);
        var_124 = wp::copy(var_125);
        // tau[dof_start + i] = -wp.dot(S_s, body_f_s) + joint_f[dof_start + i]               <L 548>
        var_126 = wp::dot(var_124, var_body_f_s);
        var_127 = wp::neg(var_126);
        var_128 = wp::add(var_dof_start, var_121);
        var_129 = wp::address(var_joint_f, var_128);
        var_131 = wp::load(var_129);
        var_130 = wp::add(var_127, var_131);
        var_132 = wp::add(var_dof_start, var_121);
        wp::array_store(var_tau, var_132, var_130);
        // return                                                                             <L 550>
        return;
    }
    var_133 = wp::where(var_56, var_121, var_38);
    var_134 = wp::where(var_56, var_124, var_41);
    // if type == JointType.PRISMATIC or type == JointType.REVOLUTE or type == JointType.D6:       <L 552>
    var_137 = (var_type == var_136);
    var_135 = var_137;
    if (!var_135) {
        var_139 = (var_type == var_138);
        var_135 = var_135 || var_139;
    }
    if (!var_135) {
        var_141 = (var_type == var_140);
        var_135 = var_135 || var_141;
    }
    if (var_135) {
        // axis_count = lin_axis_count + ang_axis_count                                       <L 553>
        var_142 = wp::add(var_lin_axis_count, var_ang_axis_count);
        // for i in range(axis_count):                                                        <L 555>
        var_143 = wp::range(var_142);
        start_for_2:;
            if (iter_cmp(var_143) == 0) goto end_for_2;
            var_144 = wp::iter_next(var_143);
            // j = dof_start + i                                                              <L 556>
            var_145 = wp::add(var_dof_start, var_144);
            // S_s = joint_S_s[j]                                                             <L 557>
            var_146 = wp::address(var_joint_S_s, var_145);
            var_148 = wp::load(var_146);
            var_147 = wp::copy(var_148);
            // q = joint_q[coord_start + i]                                                   <L 559>
            var_149 = wp::add(var_coord_start, var_144);
            var_150 = wp::address(var_joint_q, var_149);
            var_152 = wp::load(var_150);
            var_151 = wp::copy(var_152);
            // qd = joint_qd[j]                                                               <L 560>
            var_153 = wp::address(var_joint_qd, var_145);
            var_155 = wp::load(var_153);
            var_154 = wp::copy(var_155);
            // lower = joint_limit_lower[j]                                                   <L 562>
            var_156 = wp::address(var_joint_limit_lower, var_145);
            var_158 = wp::load(var_156);
            var_157 = wp::copy(var_158);
            // upper = joint_limit_upper[j]                                                   <L 563>
            var_159 = wp::address(var_joint_limit_upper, var_145);
            var_161 = wp::load(var_159);
            var_160 = wp::copy(var_161);
            // limit_ke = joint_limit_ke[j]                                                   <L 564>
            var_162 = wp::address(var_joint_limit_ke, var_145);
            var_164 = wp::load(var_162);
            var_163 = wp::copy(var_164);
            // limit_kd = joint_limit_kd[j]                                                   <L 565>
            var_165 = wp::address(var_joint_limit_kd, var_145);
            var_167 = wp::load(var_165);
            var_166 = wp::copy(var_167);
            // target_ke = joint_target_ke[j]                                                 <L 566>
            var_168 = wp::address(var_joint_target_ke, var_145);
            var_170 = wp::load(var_168);
            var_169 = wp::copy(var_170);
            // target_kd = joint_target_kd[j]                                                 <L 567>
            var_171 = wp::address(var_joint_target_kd, var_145);
            var_173 = wp::load(var_171);
            var_172 = wp::copy(var_173);
            // target_pos = joint_target_q[target_q_start + i]                                <L 568>
            var_174 = wp::add(var_target_q_start, var_144);
            var_175 = wp::address(var_joint_target_q, var_174);
            var_177 = wp::load(var_175);
            var_176 = wp::copy(var_177);
            // target_vel = joint_target_qd[j]                                                <L 569>
            var_178 = wp::address(var_joint_target_qd, var_145);
            var_180 = wp::load(var_178);
            var_179 = wp::copy(var_180);
            // damping = joint_damping[j]                                                     <L 570>
            var_181 = wp::address(var_joint_damping, var_145);
            var_183 = wp::load(var_181);
            var_182 = wp::copy(var_183);
            // drive_f = joint_force(                                                         <L 572>
            // q, qd, target_pos, target_vel, target_ke, target_kd, lower, upper, limit_ke, limit_kd, damping       <L 573>
            var_184 = joint_force_0(var_151, var_154, var_176, var_179, var_169, var_172, var_157, var_160, var_163, var_166, var_182);
            // t = -wp.dot(S_s, body_f_s) + drive_f + joint_f[j]                              <L 577>
            var_185 = wp::dot(var_147, var_body_f_s);
            var_186 = wp::neg(var_185);
            var_187 = wp::add(var_186, var_184);
            var_188 = wp::address(var_joint_f, var_145);
            var_190 = wp::load(var_188);
            var_189 = wp::add(var_187, var_190);
            // tau[j] = t                                                                     <L 579>
            wp::array_store(var_tau, var_145, var_189);
            wp::assign(var_134, var_147);
            wp::assign(var_43, var_145);
            goto start_for_2;
        end_for_2:;
        // return                                                                             <L 581>
        return;
    }
    var_191 = wp::where(var_135, var_144, var_133);
}


// newton/_src/solvers/featherstone/kernels.py:362
static void jcalc_motion_0(
    wp::int32 var_type,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::float32> var_joint_q,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::transform_t<wp::float32> var_X_sc,
    wp::array_t<wp::float32> var_joint_qd,
    wp::int32 var_q_start,
    wp::int32 var_qd_start,
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s,
    wp::vec_t<6, wp::float32> & ret_0,
    wp::vec_t<6, wp::float32> & ret_1)
{
    //---------
    // primal vars
    const wp::int32 var_0 = 0;
    bool var_1{};
    wp::vec_t<3, wp::float32>* var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    wp::vec_t<3, wp::float32> var_4{};
    wp::vec_t<3, wp::float32> var_5{};
    wp::vec_t<6, wp::float32> var_6{};
    wp::vec_t<6, wp::float32> var_7{};
    wp::float32* var_8{};
    wp::vec_t<6, wp::float32> var_9{};
    wp::float32 var_10{};
    wp::vec_t<6, wp::float32> var_11{};
    const wp::int32 var_12 = 1;
    bool var_13{};
    wp::vec_t<3, wp::float32>* var_14{};
    wp::vec_t<3, wp::float32> var_15{};
    wp::vec_t<3, wp::float32> var_16{};
    wp::vec_t<3, wp::float32> var_17{};
    wp::vec_t<6, wp::float32> var_18{};
    wp::vec_t<6, wp::float32> var_19{};
    wp::float32* var_20{};
    wp::vec_t<6, wp::float32> var_21{};
    wp::float32 var_22{};
    wp::vec_t<6, wp::float32> var_23{};
    wp::vec_t<3, wp::float32> var_24{};
    wp::vec_t<6, wp::float32> var_25{};
    wp::vec_t<6, wp::float32> var_26{};
    const wp::int32 var_27 = 6;
    bool var_28{};
    wp::vec_t<3, wp::float32> var_29{};
    wp::vec_t<6, wp::float32> var_30{};
    const wp::int32 var_31 = 0;
    bool var_32{};
    const wp::int32 var_33 = 0;
    wp::int32 var_34{};
    wp::vec_t<3, wp::float32>* var_35{};
    wp::vec_t<3, wp::float32> var_36{};
    wp::vec_t<3, wp::float32> var_37{};
    wp::vec_t<3, wp::float32> var_38{};
    wp::vec_t<6, wp::float32> var_39{};
    wp::vec_t<6, wp::float32> var_40{};
    const wp::int32 var_41 = 0;
    wp::int32 var_42{};
    wp::float32* var_43{};
    wp::vec_t<6, wp::float32> var_44{};
    wp::float32 var_45{};
    wp::vec_t<6, wp::float32> var_46{};
    const wp::int32 var_47 = 0;
    wp::int32 var_48{};
    wp::vec_t<3, wp::float32> var_49{};
    wp::vec_t<6, wp::float32> var_50{};
    wp::vec_t<6, wp::float32> var_51{};
    const wp::int32 var_52 = 1;
    bool var_53{};
    const wp::int32 var_54 = 1;
    wp::int32 var_55{};
    wp::vec_t<3, wp::float32>* var_56{};
    wp::vec_t<3, wp::float32> var_57{};
    wp::vec_t<3, wp::float32> var_58{};
    wp::vec_t<3, wp::float32> var_59{};
    wp::vec_t<6, wp::float32> var_60{};
    wp::vec_t<6, wp::float32> var_61{};
    const wp::int32 var_62 = 1;
    wp::int32 var_63{};
    wp::float32* var_64{};
    wp::vec_t<6, wp::float32> var_65{};
    wp::float32 var_66{};
    wp::vec_t<6, wp::float32> var_67{};
    const wp::int32 var_68 = 1;
    wp::int32 var_69{};
    wp::vec_t<3, wp::float32> var_70{};
    wp::vec_t<6, wp::float32> var_71{};
    wp::vec_t<6, wp::float32> var_72{};
    const wp::int32 var_73 = 2;
    bool var_74{};
    const wp::int32 var_75 = 2;
    wp::int32 var_76{};
    wp::vec_t<3, wp::float32>* var_77{};
    wp::vec_t<3, wp::float32> var_78{};
    wp::vec_t<3, wp::float32> var_79{};
    wp::vec_t<3, wp::float32> var_80{};
    wp::vec_t<6, wp::float32> var_81{};
    wp::vec_t<6, wp::float32> var_82{};
    const wp::int32 var_83 = 2;
    wp::int32 var_84{};
    wp::float32* var_85{};
    wp::vec_t<6, wp::float32> var_86{};
    wp::float32 var_87{};
    wp::vec_t<6, wp::float32> var_88{};
    const wp::int32 var_89 = 2;
    wp::int32 var_90{};
    wp::vec_t<3, wp::float32> var_91{};
    wp::vec_t<6, wp::float32> var_92{};
    wp::vec_t<6, wp::float32> var_93{};
    wp::int32 var_94{};
    wp::int32 var_95{};
    const wp::int32 var_96 = 1;
    bool var_97{};
    wp::vec_t<3, wp::float32>* var_98{};
    wp::vec_t<3, wp::float32> var_99{};
    wp::vec_t<3, wp::float32> var_100{};
    wp::vec_t<3, wp::float32> var_101{};
    wp::vec_t<6, wp::float32> var_102{};
    wp::vec_t<6, wp::float32> var_103{};
    wp::float32* var_104{};
    wp::vec_t<6, wp::float32> var_105{};
    wp::float32 var_106{};
    wp::vec_t<6, wp::float32> var_107{};
    wp::vec_t<3, wp::float32> var_108{};
    wp::vec_t<6, wp::float32> var_109{};
    wp::vec_t<6, wp::float32> var_110{};
    const wp::int32 var_111 = 2;
    bool var_112{};
    const wp::int32 var_113 = 0;
    wp::int32 var_114{};
    wp::vec_t<3, wp::float32>* var_115{};
    const wp::int32 var_116 = 1;
    wp::int32 var_117{};
    wp::vec_t<3, wp::float32>* var_118{};
    const wp::int32 var_119 = 0;
    wp::int32 var_120{};
    wp::float32* var_121{};
    wp::vec_t<3, wp::float32> var_122{};
    wp::vec_t<3, wp::float32> var_123{};
    wp::vec_t<3, wp::float32> var_124{};
    wp::vec_t<3, wp::float32> var_125{};
    wp::float32 var_126{};
    wp::vec_t<3, wp::float32> var_127{};
    wp::vec_t<6, wp::float32> var_128{};
    wp::vec_t<6, wp::float32> var_129{};
    wp::vec_t<3, wp::float32> var_130{};
    wp::vec_t<6, wp::float32> var_131{};
    wp::vec_t<6, wp::float32> var_132{};
    const wp::int32 var_133 = 0;
    wp::int32 var_134{};
    wp::float32* var_135{};
    wp::float32 var_136{};
    wp::float32 var_137{};
    const wp::int32 var_138 = 1;
    wp::int32 var_139{};
    wp::float32* var_140{};
    wp::float32 var_141{};
    wp::float32 var_142{};
    wp::vec_t<6, wp::float32> var_143{};
    wp::vec_t<6, wp::float32> var_144{};
    wp::vec_t<6, wp::float32> var_145{};
    wp::vec_t<6, wp::float32> var_146{};
    const wp::int32 var_147 = 0;
    wp::int32 var_148{};
    const wp::int32 var_149 = 1;
    wp::int32 var_150{};
    wp::vec_t<3, wp::float32> var_151{};
    wp::float32 var_152{};
    wp::vec_t<3, wp::float32> var_153{};
    wp::vec_t<3, wp::float32> var_154{};
    wp::vec_t<6, wp::float32> var_155{};
    wp::vec_t<3, wp::float32> var_156{};
    const wp::int32 var_157 = 3;
    bool var_158{};
    const wp::int32 var_159 = 0;
    wp::int32 var_160{};
    wp::vec_t<3, wp::float32>* var_161{};
    const wp::int32 var_162 = 1;
    wp::int32 var_163{};
    wp::vec_t<3, wp::float32>* var_164{};
    const wp::int32 var_165 = 2;
    wp::int32 var_166{};
    wp::vec_t<3, wp::float32>* var_167{};
    const wp::int32 var_168 = 0;
    wp::int32 var_169{};
    wp::float32* var_170{};
    const wp::int32 var_171 = 1;
    wp::int32 var_172{};
    wp::float32* var_173{};
    wp::vec_t<3, wp::float32> var_174{};
    wp::vec_t<3, wp::float32> var_175{};
    wp::vec_t<3, wp::float32> var_176{};
    wp::vec_t<3, wp::float32> var_177{};
    wp::vec_t<3, wp::float32> var_178{};
    wp::vec_t<3, wp::float32> var_179{};
    wp::float32 var_180{};
    wp::float32 var_181{};
    wp::vec_t<3, wp::float32> var_182{};
    wp::vec_t<6, wp::float32> var_183{};
    wp::vec_t<6, wp::float32> var_184{};
    wp::vec_t<3, wp::float32> var_185{};
    wp::vec_t<6, wp::float32> var_186{};
    wp::vec_t<6, wp::float32> var_187{};
    wp::vec_t<3, wp::float32> var_188{};
    wp::vec_t<6, wp::float32> var_189{};
    wp::vec_t<6, wp::float32> var_190{};
    const wp::int32 var_191 = 0;
    wp::int32 var_192{};
    wp::float32* var_193{};
    wp::float32 var_194{};
    wp::float32 var_195{};
    const wp::int32 var_196 = 1;
    wp::int32 var_197{};
    wp::float32* var_198{};
    wp::float32 var_199{};
    wp::float32 var_200{};
    const wp::int32 var_201 = 2;
    wp::int32 var_202{};
    wp::float32* var_203{};
    wp::float32 var_204{};
    wp::float32 var_205{};
    wp::vec_t<6, wp::float32> var_206{};
    wp::vec_t<6, wp::float32> var_207{};
    wp::vec_t<6, wp::float32> var_208{};
    wp::vec_t<6, wp::float32> var_209{};
    wp::vec_t<6, wp::float32> var_210{};
    wp::vec_t<6, wp::float32> var_211{};
    const wp::int32 var_212 = 0;
    wp::int32 var_213{};
    const wp::int32 var_214 = 1;
    wp::int32 var_215{};
    const wp::int32 var_216 = 2;
    wp::int32 var_217{};
    wp::vec_t<3, wp::float32> var_218{};
    wp::float32 var_219{};
    wp::vec_t<3, wp::float32> var_220{};
    wp::vec_t<3, wp::float32> var_221{};
    wp::vec_t<3, wp::float32> var_222{};
    wp::float32 var_223{};
    wp::vec_t<3, wp::float32> var_224{};
    wp::vec_t<3, wp::float32> var_225{};
    wp::vec_t<3, wp::float32> var_226{};
    wp::float32 var_227{};
    wp::vec_t<3, wp::float32> var_228{};
    wp::vec_t<3, wp::float32> var_229{};
    wp::vec_t<6, wp::float32> var_230{};
    wp::vec_t<3, wp::float32> var_231{};
    wp::vec_t<3, wp::float32> var_232{};
    wp::vec_t<3, wp::float32> var_233{};
    wp::vec_t<6, wp::float32> var_234{};
    wp::vec_t<6, wp::float32> var_235{};
    wp::float32 var_236{};
    wp::float32 var_237{};
    wp::vec_t<3, wp::float32> var_238{};
    wp::vec_t<6, wp::float32> var_239{};
    wp::vec_t<6, wp::float32> var_240{};
    wp::vec_t<3, wp::float32> var_241{};
    wp::vec_t<6, wp::float32> var_242{};
    wp::vec_t<6, wp::float32> var_243{};
    const wp::int32 var_244 = 2;
    bool var_245{};
    const wp::float32 var_246 = 0.0;
    const wp::float32 var_247 = 0.0;
    const wp::float32 var_248 = 0.0;
    const wp::float32 var_249 = 1.0;
    const wp::float32 var_250 = 0.0;
    const wp::float32 var_251 = 0.0;
    wp::vec_t<6, wp::float32> var_252{};
    wp::vec_t<6, wp::float32> var_253{};
    const wp::float32 var_254 = 0.0;
    const wp::float32 var_255 = 0.0;
    const wp::float32 var_256 = 0.0;
    const wp::float32 var_257 = 0.0;
    const wp::float32 var_258 = 1.0;
    const wp::float32 var_259 = 0.0;
    wp::vec_t<6, wp::float32> var_260{};
    wp::vec_t<6, wp::float32> var_261{};
    const wp::float32 var_262 = 0.0;
    const wp::float32 var_263 = 0.0;
    const wp::float32 var_264 = 0.0;
    const wp::float32 var_265 = 0.0;
    const wp::float32 var_266 = 0.0;
    const wp::float32 var_267 = 1.0;
    wp::vec_t<6, wp::float32> var_268{};
    wp::vec_t<6, wp::float32> var_269{};
    const wp::int32 var_270 = 0;
    wp::int32 var_271{};
    const wp::int32 var_272 = 1;
    wp::int32 var_273{};
    const wp::int32 var_274 = 2;
    wp::int32 var_275{};
    const wp::int32 var_276 = 0;
    wp::int32 var_277{};
    wp::float32* var_278{};
    wp::vec_t<6, wp::float32> var_279{};
    wp::float32 var_280{};
    const wp::int32 var_281 = 1;
    wp::int32 var_282{};
    wp::float32* var_283{};
    wp::vec_t<6, wp::float32> var_284{};
    wp::float32 var_285{};
    wp::vec_t<6, wp::float32> var_286{};
    const wp::int32 var_287 = 2;
    wp::int32 var_288{};
    wp::float32* var_289{};
    wp::vec_t<6, wp::float32> var_290{};
    wp::float32 var_291{};
    wp::vec_t<6, wp::float32> var_292{};
    wp::vec_t<6, wp::float32> var_293{};
    wp::vec_t<6, wp::float32> var_294{};
    wp::vec_t<6, wp::float32> var_295{};
    wp::vec_t<6, wp::float32> var_296{};
    const wp::int32 var_297 = 3;
    bool var_298{};
    wp::vec_t<6, wp::float32> var_299{};
    wp::vec_t<6, wp::float32> var_300{};
    bool var_301{};
    const wp::int32 var_302 = 4;
    bool var_303{};
    const wp::int32 var_304 = 5;
    bool var_305{};
    const wp::int32 var_306 = 0;
    wp::int32 var_307{};
    wp::float32* var_308{};
    const wp::int32 var_309 = 1;
    wp::int32 var_310{};
    wp::float32* var_311{};
    const wp::int32 var_312 = 2;
    wp::int32 var_313{};
    wp::float32* var_314{};
    const wp::int32 var_315 = 3;
    wp::int32 var_316{};
    wp::float32* var_317{};
    const wp::int32 var_318 = 4;
    wp::int32 var_319{};
    wp::float32* var_320{};
    const wp::int32 var_321 = 5;
    wp::int32 var_322{};
    wp::float32* var_323{};
    wp::vec_t<6, wp::float32> var_324{};
    wp::float32 var_325{};
    wp::float32 var_326{};
    wp::float32 var_327{};
    wp::float32 var_328{};
    wp::float32 var_329{};
    wp::float32 var_330{};
    wp::vec_t<6, wp::float32> var_331{};
    const wp::float32 var_332 = 1.0;
    const wp::float32 var_333 = 0.0;
    const wp::float32 var_334 = 0.0;
    const wp::float32 var_335 = 0.0;
    const wp::float32 var_336 = 0.0;
    const wp::float32 var_337 = 0.0;
    wp::vec_t<6, wp::float32> var_338{};
    wp::vec_t<6, wp::float32> var_339{};
    const wp::int32 var_340 = 0;
    wp::int32 var_341{};
    const wp::float32 var_342 = 0.0;
    const wp::float32 var_343 = 1.0;
    const wp::float32 var_344 = 0.0;
    const wp::float32 var_345 = 0.0;
    const wp::float32 var_346 = 0.0;
    const wp::float32 var_347 = 0.0;
    wp::vec_t<6, wp::float32> var_348{};
    wp::vec_t<6, wp::float32> var_349{};
    const wp::int32 var_350 = 1;
    wp::int32 var_351{};
    const wp::float32 var_352 = 0.0;
    const wp::float32 var_353 = 0.0;
    const wp::float32 var_354 = 1.0;
    const wp::float32 var_355 = 0.0;
    const wp::float32 var_356 = 0.0;
    const wp::float32 var_357 = 0.0;
    wp::vec_t<6, wp::float32> var_358{};
    wp::vec_t<6, wp::float32> var_359{};
    const wp::int32 var_360 = 2;
    wp::int32 var_361{};
    const wp::float32 var_362 = 0.0;
    const wp::float32 var_363 = 0.0;
    const wp::float32 var_364 = 0.0;
    const wp::float32 var_365 = 1.0;
    const wp::float32 var_366 = 0.0;
    const wp::float32 var_367 = 0.0;
    wp::vec_t<6, wp::float32> var_368{};
    wp::vec_t<6, wp::float32> var_369{};
    const wp::int32 var_370 = 3;
    wp::int32 var_371{};
    const wp::float32 var_372 = 0.0;
    const wp::float32 var_373 = 0.0;
    const wp::float32 var_374 = 0.0;
    const wp::float32 var_375 = 0.0;
    const wp::float32 var_376 = 1.0;
    const wp::float32 var_377 = 0.0;
    wp::vec_t<6, wp::float32> var_378{};
    wp::vec_t<6, wp::float32> var_379{};
    const wp::int32 var_380 = 4;
    wp::int32 var_381{};
    const wp::float32 var_382 = 0.0;
    const wp::float32 var_383 = 0.0;
    const wp::float32 var_384 = 0.0;
    const wp::float32 var_385 = 0.0;
    const wp::float32 var_386 = 0.0;
    const wp::float32 var_387 = 1.0;
    wp::vec_t<6, wp::float32> var_388{};
    wp::vec_t<6, wp::float32> var_389{};
    const wp::int32 var_390 = 5;
    wp::int32 var_391{};
    wp::vec_t<6, wp::float32> var_392{};
    wp::vec_t<6, wp::float32> var_393{};
    const wp::str var_394 = "jcalc_motion not implemented for joint type %d\n";
    wp::vec_t<6, wp::float32> var_395{};
    wp::vec_t<6, wp::float32> var_396{};
    //---------
    // forward
    // def jcalc_motion(                                                                      <L 363>
    // if type == JointType.PRISMATIC:                                                        <L 376>
    var_1 = (var_type == var_0);
    if (var_1) {
        // axis = joint_axis[qd_start]                                                        <L 377>
        var_2 = wp::address(var_joint_axis, var_qd_start);
        var_4 = wp::load(var_2);
        var_3 = wp::copy(var_4);
        // S_s = transform_twist(X_sc, wp.spatial_vector(axis, wp.vec3()))                    <L 378>
        var_5 = wp::vec_t<3, wp::float32>();
        var_6 = wp::vec_t<6, wp::float32>(var_3, var_5);
        var_7 = transform_twist_1(var_X_sc, var_6);
        // v_j_s = S_s * joint_qd[qd_start]                                                   <L 379>
        var_8 = wp::address(var_joint_qd, var_qd_start);
        var_10 = wp::load(var_8);
        var_9 = wp::mul(var_7, var_10);
        // joint_S_s[qd_start] = S_s                                                          <L 380>
        wp::array_store(var_joint_S_s, var_qd_start, var_7);
        // return v_j_s, wp.spatial_vector()                                                  <L 381>
        var_11 = wp::vec_t<6, wp::float32>();
        ret_0 = var_9;
        ret_1 = var_11;
        return;
    }
    // if type == JointType.REVOLUTE:                                                         <L 383>
    var_13 = (var_type == var_12);
    if (var_13) {
        // axis = joint_axis[qd_start]                                                        <L 384>
        var_14 = wp::address(var_joint_axis, var_qd_start);
        var_16 = wp::load(var_14);
        var_15 = wp::copy(var_16);
        // S_s = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), axis))                    <L 385>
        var_17 = wp::vec_t<3, wp::float32>();
        var_18 = wp::vec_t<6, wp::float32>(var_17, var_15);
        var_19 = transform_twist_1(var_X_sc, var_18);
        // v_j_s = S_s * joint_qd[qd_start]                                                   <L 386>
        var_20 = wp::address(var_joint_qd, var_qd_start);
        var_22 = wp::load(var_20);
        var_21 = wp::mul(var_19, var_22);
        // joint_S_s[qd_start] = S_s                                                          <L 387>
        wp::array_store(var_joint_S_s, var_qd_start, var_19);
        // return v_j_s, wp.spatial_vector()                                                  <L 388>
        var_23 = wp::vec_t<6, wp::float32>();
        ret_0 = var_21;
        ret_1 = var_23;
        return;
    }
    var_24 = wp::where(var_13, var_15, var_3);
    var_25 = wp::where(var_13, var_19, var_7);
    var_26 = wp::where(var_13, var_21, var_9);
    // if type == JointType.D6:                                                               <L 390>
    var_28 = (var_type == var_27);
    if (var_28) {
        // c_app_ang = wp.vec3()                                                              <L 394>
        var_29 = wp::vec_t<3, wp::float32>();
        // v_j_s = wp.spatial_vector()                                                        <L 395>
        var_30 = wp::vec_t<6, wp::float32>();
        // if lin_axis_count > 0:                                                             <L 396>
        var_32 = (var_lin_axis_count > var_31);
        if (var_32) {
            // axis = joint_axis[qd_start + 0]                                                <L 397>
            var_34 = wp::add(var_qd_start, var_33);
            var_35 = wp::address(var_joint_axis, var_34);
            var_37 = wp::load(var_35);
            var_36 = wp::copy(var_37);
            // S_s = transform_twist(X_sc, wp.spatial_vector(axis, wp.vec3()))                <L 398>
            var_38 = wp::vec_t<3, wp::float32>();
            var_39 = wp::vec_t<6, wp::float32>(var_36, var_38);
            var_40 = transform_twist_1(var_X_sc, var_39);
            // v_j_s += S_s * joint_qd[qd_start + 0]                                          <L 399>
            var_42 = wp::add(var_qd_start, var_41);
            var_43 = wp::address(var_joint_qd, var_42);
            var_45 = wp::load(var_43);
            var_44 = wp::mul(var_40, var_45);
            var_46 = wp::add(var_30, var_44);
            // joint_S_s[qd_start + 0] = S_s                                                  <L 400>
            var_48 = wp::add(var_qd_start, var_47);
            wp::array_store(var_joint_S_s, var_48, var_40);
        }
        var_49 = wp::where(var_32, var_36, var_24);
        var_50 = wp::where(var_32, var_40, var_25);
        var_51 = wp::where(var_32, var_46, var_30);
        // if lin_axis_count > 1:                                                             <L 401>
        var_53 = (var_lin_axis_count > var_52);
        if (var_53) {
            // axis = joint_axis[qd_start + 1]                                                <L 402>
            var_55 = wp::add(var_qd_start, var_54);
            var_56 = wp::address(var_joint_axis, var_55);
            var_58 = wp::load(var_56);
            var_57 = wp::copy(var_58);
            // S_s = transform_twist(X_sc, wp.spatial_vector(axis, wp.vec3()))                <L 403>
            var_59 = wp::vec_t<3, wp::float32>();
            var_60 = wp::vec_t<6, wp::float32>(var_57, var_59);
            var_61 = transform_twist_1(var_X_sc, var_60);
            // v_j_s += S_s * joint_qd[qd_start + 1]                                          <L 404>
            var_63 = wp::add(var_qd_start, var_62);
            var_64 = wp::address(var_joint_qd, var_63);
            var_66 = wp::load(var_64);
            var_65 = wp::mul(var_61, var_66);
            var_67 = wp::add(var_51, var_65);
            // joint_S_s[qd_start + 1] = S_s                                                  <L 405>
            var_69 = wp::add(var_qd_start, var_68);
            wp::array_store(var_joint_S_s, var_69, var_61);
        }
        var_70 = wp::where(var_53, var_57, var_49);
        var_71 = wp::where(var_53, var_61, var_50);
        var_72 = wp::where(var_53, var_67, var_51);
        // if lin_axis_count > 2:                                                             <L 406>
        var_74 = (var_lin_axis_count > var_73);
        if (var_74) {
            // axis = joint_axis[qd_start + 2]                                                <L 407>
            var_76 = wp::add(var_qd_start, var_75);
            var_77 = wp::address(var_joint_axis, var_76);
            var_79 = wp::load(var_77);
            var_78 = wp::copy(var_79);
            // S_s = transform_twist(X_sc, wp.spatial_vector(axis, wp.vec3()))                <L 408>
            var_80 = wp::vec_t<3, wp::float32>();
            var_81 = wp::vec_t<6, wp::float32>(var_78, var_80);
            var_82 = transform_twist_1(var_X_sc, var_81);
            // v_j_s += S_s * joint_qd[qd_start + 2]                                          <L 409>
            var_84 = wp::add(var_qd_start, var_83);
            var_85 = wp::address(var_joint_qd, var_84);
            var_87 = wp::load(var_85);
            var_86 = wp::mul(var_82, var_87);
            var_88 = wp::add(var_72, var_86);
            // joint_S_s[qd_start + 2] = S_s                                                  <L 410>
            var_90 = wp::add(var_qd_start, var_89);
            wp::array_store(var_joint_S_s, var_90, var_82);
        }
        var_91 = wp::where(var_74, var_78, var_70);
        var_92 = wp::where(var_74, var_82, var_71);
        var_93 = wp::where(var_74, var_88, var_72);
        // iqd = qd_start + lin_axis_count                                                    <L 413>
        var_94 = wp::add(var_qd_start, var_lin_axis_count);
        // iq = q_start + lin_axis_count                                                      <L 414>
        var_95 = wp::add(var_q_start, var_lin_axis_count);
        // if ang_axis_count == 1:                                                            <L 415>
        var_97 = (var_ang_axis_count == var_96);
        if (var_97) {
            // axis = joint_axis[iqd]                                                         <L 416>
            var_98 = wp::address(var_joint_axis, var_94);
            var_100 = wp::load(var_98);
            var_99 = wp::copy(var_100);
            // S_s = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), axis))                <L 417>
            var_101 = wp::vec_t<3, wp::float32>();
            var_102 = wp::vec_t<6, wp::float32>(var_101, var_99);
            var_103 = transform_twist_1(var_X_sc, var_102);
            // v_j_s += S_s * joint_qd[iqd]                                                   <L 418>
            var_104 = wp::address(var_joint_qd, var_94);
            var_106 = wp::load(var_104);
            var_105 = wp::mul(var_103, var_106);
            var_107 = wp::add(var_93, var_105);
            // joint_S_s[iqd] = S_s                                                           <L 419>
            wp::array_store(var_joint_S_s, var_94, var_103);
        }
        var_108 = wp::where(var_97, var_99, var_91);
        var_109 = wp::where(var_97, var_103, var_92);
        var_110 = wp::where(var_97, var_107, var_93);
        // if ang_axis_count == 2:                                                            <L 420>
        var_112 = (var_ang_axis_count == var_111);
        if (var_112) {
            // a0, a1 = transform_2d_rotational_axes(joint_axis[iqd + 0], joint_axis[iqd + 1], joint_q[iq + 0])       <L 421>
            var_114 = wp::add(var_94, var_113);
            var_115 = wp::address(var_joint_axis, var_114);
            var_117 = wp::add(var_94, var_116);
            var_118 = wp::address(var_joint_axis, var_117);
            var_120 = wp::add(var_95, var_119);
            var_121 = wp::address(var_joint_q, var_120);
            var_124 = wp::load(var_115);
            var_125 = wp::load(var_118);
            var_126 = wp::load(var_121);
            transform_2d_rotational_axes_0(var_124, var_125, var_126, var_122, var_123);
            // S_0 = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), a0))                  <L 422>
            var_127 = wp::vec_t<3, wp::float32>();
            var_128 = wp::vec_t<6, wp::float32>(var_127, var_122);
            var_129 = transform_twist_1(var_X_sc, var_128);
            // S_1 = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), a1))                  <L 423>
            var_130 = wp::vec_t<3, wp::float32>();
            var_131 = wp::vec_t<6, wp::float32>(var_130, var_123);
            var_132 = transform_twist_1(var_X_sc, var_131);
            // qd0 = joint_qd[iqd + 0]                                                        <L 424>
            var_134 = wp::add(var_94, var_133);
            var_135 = wp::address(var_joint_qd, var_134);
            var_137 = wp::load(var_135);
            var_136 = wp::copy(var_137);
            // qd1 = joint_qd[iqd + 1]                                                        <L 425>
            var_139 = wp::add(var_94, var_138);
            var_140 = wp::address(var_joint_qd, var_139);
            var_142 = wp::load(var_140);
            var_141 = wp::copy(var_142);
            // v_j_s += S_0 * qd0 + S_1 * qd1                                                 <L 426>
            var_143 = wp::mul(var_129, var_136);
            var_144 = wp::mul(var_132, var_141);
            var_145 = wp::add(var_143, var_144);
            var_146 = wp::add(var_110, var_145);
            // joint_S_s[iqd + 0] = S_0                                                       <L 427>
            var_148 = wp::add(var_94, var_147);
            wp::array_store(var_joint_S_s, var_148, var_129);
            // joint_S_s[iqd + 1] = S_1                                                       <L 428>
            var_150 = wp::add(var_94, var_149);
            wp::array_store(var_joint_S_s, var_150, var_132);
            // c_app_ang += wp.cross(a0, a1) * (qd0 * qd1)                                    <L 430>
            var_151 = wp::cross(var_122, var_123);
            var_152 = wp::mul(var_136, var_141);
            var_153 = wp::mul(var_151, var_152);
            var_154 = wp::add(var_29, var_153);
        }
        var_155 = wp::where(var_112, var_146, var_110);
        var_156 = wp::where(var_112, var_154, var_29);
        // if ang_axis_count == 3:                                                            <L 431>
        var_158 = (var_ang_axis_count == var_157);
        if (var_158) {
            // a0, a1, a2 = transform_3d_rotational_axes(                                     <L 432>
            // joint_axis[iqd + 0],                                                           <L 433>
            var_160 = wp::add(var_94, var_159);
            var_161 = wp::address(var_joint_axis, var_160);
            // joint_axis[iqd + 1],                                                           <L 434>
            var_163 = wp::add(var_94, var_162);
            var_164 = wp::address(var_joint_axis, var_163);
            // joint_axis[iqd + 2],                                                           <L 435>
            var_166 = wp::add(var_94, var_165);
            var_167 = wp::address(var_joint_axis, var_166);
            // joint_q[iq + 0],                                                               <L 436>
            var_169 = wp::add(var_95, var_168);
            var_170 = wp::address(var_joint_q, var_169);
            // joint_q[iq + 1],                                                               <L 437>
            var_172 = wp::add(var_95, var_171);
            var_173 = wp::address(var_joint_q, var_172);
            var_177 = wp::load(var_161);
            var_178 = wp::load(var_164);
            var_179 = wp::load(var_167);
            var_180 = wp::load(var_170);
            var_181 = wp::load(var_173);
            transform_3d_rotational_axes_0(var_177, var_178, var_179, var_180, var_181, var_174, var_175, var_176);
            // S_0 = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), a0))                  <L 439>
            var_182 = wp::vec_t<3, wp::float32>();
            var_183 = wp::vec_t<6, wp::float32>(var_182, var_174);
            var_184 = transform_twist_1(var_X_sc, var_183);
            // S_1 = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), a1))                  <L 440>
            var_185 = wp::vec_t<3, wp::float32>();
            var_186 = wp::vec_t<6, wp::float32>(var_185, var_175);
            var_187 = transform_twist_1(var_X_sc, var_186);
            // S_2 = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), a2))                  <L 441>
            var_188 = wp::vec_t<3, wp::float32>();
            var_189 = wp::vec_t<6, wp::float32>(var_188, var_176);
            var_190 = transform_twist_1(var_X_sc, var_189);
            // qd0 = joint_qd[iqd + 0]                                                        <L 442>
            var_192 = wp::add(var_94, var_191);
            var_193 = wp::address(var_joint_qd, var_192);
            var_195 = wp::load(var_193);
            var_194 = wp::copy(var_195);
            // qd1 = joint_qd[iqd + 1]                                                        <L 443>
            var_197 = wp::add(var_94, var_196);
            var_198 = wp::address(var_joint_qd, var_197);
            var_200 = wp::load(var_198);
            var_199 = wp::copy(var_200);
            // qd2 = joint_qd[iqd + 2]                                                        <L 444>
            var_202 = wp::add(var_94, var_201);
            var_203 = wp::address(var_joint_qd, var_202);
            var_205 = wp::load(var_203);
            var_204 = wp::copy(var_205);
            // v_j_s += S_0 * qd0 + S_1 * qd1 + S_2 * qd2                                     <L 445>
            var_206 = wp::mul(var_184, var_194);
            var_207 = wp::mul(var_187, var_199);
            var_208 = wp::add(var_206, var_207);
            var_209 = wp::mul(var_190, var_204);
            var_210 = wp::add(var_208, var_209);
            var_211 = wp::add(var_155, var_210);
            // joint_S_s[iqd + 0] = S_0                                                       <L 446>
            var_213 = wp::add(var_94, var_212);
            wp::array_store(var_joint_S_s, var_213, var_184);
            // joint_S_s[iqd + 1] = S_1                                                       <L 447>
            var_215 = wp::add(var_94, var_214);
            wp::array_store(var_joint_S_s, var_215, var_187);
            // joint_S_s[iqd + 2] = S_2                                                       <L 448>
            var_217 = wp::add(var_94, var_216);
            wp::array_store(var_joint_S_s, var_217, var_190);
            // c_app_ang += wp.cross(a0, a1) * (qd0 * qd1)                                    <L 450>
            var_218 = wp::cross(var_174, var_175);
            var_219 = wp::mul(var_194, var_199);
            var_220 = wp::mul(var_218, var_219);
            var_221 = wp::add(var_156, var_220);
            // c_app_ang += wp.cross(a0, a2) * (qd0 * qd2)                                    <L 451>
            var_222 = wp::cross(var_174, var_176);
            var_223 = wp::mul(var_194, var_204);
            var_224 = wp::mul(var_222, var_223);
            var_225 = wp::add(var_221, var_224);
            // c_app_ang += wp.cross(a1, a2) * (qd1 * qd2)                                    <L 452>
            var_226 = wp::cross(var_175, var_176);
            var_227 = wp::mul(var_199, var_204);
            var_228 = wp::mul(var_226, var_227);
            var_229 = wp::add(var_225, var_228);
        }
        var_230 = wp::where(var_158, var_211, var_155);
        var_231 = wp::where(var_158, var_229, var_156);
        var_232 = wp::where(var_158, var_174, var_122);
        var_233 = wp::where(var_158, var_175, var_123);
        var_234 = wp::where(var_158, var_184, var_129);
        var_235 = wp::where(var_158, var_187, var_132);
        var_236 = wp::where(var_158, var_194, var_136);
        var_237 = wp::where(var_158, var_199, var_141);
        // c_app_s = transform_twist(X_sc, wp.spatial_vector(wp.vec3(), c_app_ang))           <L 454>
        var_238 = wp::vec_t<3, wp::float32>();
        var_239 = wp::vec_t<6, wp::float32>(var_238, var_231);
        var_240 = transform_twist_1(var_X_sc, var_239);
        // return v_j_s, c_app_s                                                              <L 455>
        ret_0 = var_230;
        ret_1 = var_240;
        return;
    }
    var_241 = wp::where(var_28, var_108, var_24);
    var_242 = wp::where(var_28, var_109, var_25);
    var_243 = wp::where(var_28, var_230, var_26);
    // if type == JointType.BALL:                                                             <L 457>
    var_245 = (var_type == var_244);
    if (var_245) {
        // S_0 = transform_twist(X_sc, wp.spatial_vector(0.0, 0.0, 0.0, 1.0, 0.0, 0.0))       <L 458>
        var_252 = wp::vec_t<6, wp::float32>({var_246, var_247, var_248, var_249, var_250, var_251});
        var_253 = transform_twist_1(var_X_sc, var_252);
        // S_1 = transform_twist(X_sc, wp.spatial_vector(0.0, 0.0, 0.0, 0.0, 1.0, 0.0))       <L 459>
        var_260 = wp::vec_t<6, wp::float32>({var_254, var_255, var_256, var_257, var_258, var_259});
        var_261 = transform_twist_1(var_X_sc, var_260);
        // S_2 = transform_twist(X_sc, wp.spatial_vector(0.0, 0.0, 0.0, 0.0, 0.0, 1.0))       <L 460>
        var_268 = wp::vec_t<6, wp::float32>({var_262, var_263, var_264, var_265, var_266, var_267});
        var_269 = transform_twist_1(var_X_sc, var_268);
        // joint_S_s[qd_start + 0] = S_0                                                      <L 462>
        var_271 = wp::add(var_qd_start, var_270);
        wp::array_store(var_joint_S_s, var_271, var_253);
        // joint_S_s[qd_start + 1] = S_1                                                      <L 463>
        var_273 = wp::add(var_qd_start, var_272);
        wp::array_store(var_joint_S_s, var_273, var_261);
        // joint_S_s[qd_start + 2] = S_2                                                      <L 464>
        var_275 = wp::add(var_qd_start, var_274);
        wp::array_store(var_joint_S_s, var_275, var_269);
        // return S_0 * joint_qd[qd_start + 0] + S_1 * joint_qd[qd_start + 1] + S_2 * joint_qd[qd_start + 2], (       <L 467>
        var_277 = wp::add(var_qd_start, var_276);
        var_278 = wp::address(var_joint_qd, var_277);
        var_280 = wp::load(var_278);
        var_279 = wp::mul(var_253, var_280);
        var_282 = wp::add(var_qd_start, var_281);
        var_283 = wp::address(var_joint_qd, var_282);
        var_285 = wp::load(var_283);
        var_284 = wp::mul(var_261, var_285);
        var_286 = wp::add(var_279, var_284);
        var_288 = wp::add(var_qd_start, var_287);
        var_289 = wp::address(var_joint_qd, var_288);
        var_291 = wp::load(var_289);
        var_290 = wp::mul(var_269, var_291);
        var_292 = wp::add(var_286, var_290);
        // wp.spatial_vector()                                                                <L 468>
        var_293 = wp::vec_t<6, wp::float32>();
        ret_0 = var_292;
        ret_1 = var_293;
        return;
    }
    var_294 = wp::where(var_245, var_253, var_234);
    var_295 = wp::where(var_245, var_261, var_235);
    var_296 = wp::where(var_245, var_269, var_190);
    // if type == JointType.FIXED:                                                            <L 471>
    var_298 = (var_type == var_297);
    if (var_298) {
        // return wp.spatial_vector(), wp.spatial_vector()                                    <L 472>
        var_299 = wp::vec_t<6, wp::float32>();
        var_300 = wp::vec_t<6, wp::float32>();
        ret_0 = var_299;
        ret_1 = var_300;
        return;
    }
    // if type == JointType.FREE or type == JointType.DISTANCE:                               <L 474>
    var_303 = (var_type == var_302);
    var_301 = var_303;
    if (!var_301) {
        var_305 = (var_type == var_304);
        var_301 = var_301 || var_305;
    }
    if (var_301) {
        // v_j_s = transform_twist(                                                           <L 475>
        // X_sc,                                                                              <L 476>
        // wp.spatial_vector(                                                                 <L 477>
        // joint_qd[qd_start + 0],                                                            <L 478>
        var_307 = wp::add(var_qd_start, var_306);
        var_308 = wp::address(var_joint_qd, var_307);
        // joint_qd[qd_start + 1],                                                            <L 479>
        var_310 = wp::add(var_qd_start, var_309);
        var_311 = wp::address(var_joint_qd, var_310);
        // joint_qd[qd_start + 2],                                                            <L 480>
        var_313 = wp::add(var_qd_start, var_312);
        var_314 = wp::address(var_joint_qd, var_313);
        // joint_qd[qd_start + 3],                                                            <L 481>
        var_316 = wp::add(var_qd_start, var_315);
        var_317 = wp::address(var_joint_qd, var_316);
        // joint_qd[qd_start + 4],                                                            <L 482>
        var_319 = wp::add(var_qd_start, var_318);
        var_320 = wp::address(var_joint_qd, var_319);
        // joint_qd[qd_start + 5],                                                            <L 483>
        var_322 = wp::add(var_qd_start, var_321);
        var_323 = wp::address(var_joint_qd, var_322);
        var_325 = wp::load(var_308);
        var_326 = wp::load(var_311);
        var_327 = wp::load(var_314);
        var_328 = wp::load(var_317);
        var_329 = wp::load(var_320);
        var_330 = wp::load(var_323);
        var_324 = wp::vec_t<6, wp::float32>({var_325, var_326, var_327, var_328, var_329, var_330});
        var_331 = transform_twist_1(var_X_sc, var_324);
        // joint_S_s[qd_start + 0] = transform_twist(X_sc, wp.spatial_vector(1.0, 0.0, 0.0, 0.0, 0.0, 0.0))       <L 487>
        var_338 = wp::vec_t<6, wp::float32>({var_332, var_333, var_334, var_335, var_336, var_337});
        var_339 = transform_twist_1(var_X_sc, var_338);
        var_341 = wp::add(var_qd_start, var_340);
        wp::array_store(var_joint_S_s, var_341, var_339);
        // joint_S_s[qd_start + 1] = transform_twist(X_sc, wp.spatial_vector(0.0, 1.0, 0.0, 0.0, 0.0, 0.0))       <L 488>
        var_348 = wp::vec_t<6, wp::float32>({var_342, var_343, var_344, var_345, var_346, var_347});
        var_349 = transform_twist_1(var_X_sc, var_348);
        var_351 = wp::add(var_qd_start, var_350);
        wp::array_store(var_joint_S_s, var_351, var_349);
        // joint_S_s[qd_start + 2] = transform_twist(X_sc, wp.spatial_vector(0.0, 0.0, 1.0, 0.0, 0.0, 0.0))       <L 489>
        var_358 = wp::vec_t<6, wp::float32>({var_352, var_353, var_354, var_355, var_356, var_357});
        var_359 = transform_twist_1(var_X_sc, var_358);
        var_361 = wp::add(var_qd_start, var_360);
        wp::array_store(var_joint_S_s, var_361, var_359);
        // joint_S_s[qd_start + 3] = transform_twist(X_sc, wp.spatial_vector(0.0, 0.0, 0.0, 1.0, 0.0, 0.0))       <L 490>
        var_368 = wp::vec_t<6, wp::float32>({var_362, var_363, var_364, var_365, var_366, var_367});
        var_369 = transform_twist_1(var_X_sc, var_368);
        var_371 = wp::add(var_qd_start, var_370);
        wp::array_store(var_joint_S_s, var_371, var_369);
        // joint_S_s[qd_start + 4] = transform_twist(X_sc, wp.spatial_vector(0.0, 0.0, 0.0, 0.0, 1.0, 0.0))       <L 491>
        var_378 = wp::vec_t<6, wp::float32>({var_372, var_373, var_374, var_375, var_376, var_377});
        var_379 = transform_twist_1(var_X_sc, var_378);
        var_381 = wp::add(var_qd_start, var_380);
        wp::array_store(var_joint_S_s, var_381, var_379);
        // joint_S_s[qd_start + 5] = transform_twist(X_sc, wp.spatial_vector(0.0, 0.0, 0.0, 0.0, 0.0, 1.0))       <L 492>
        var_388 = wp::vec_t<6, wp::float32>({var_382, var_383, var_384, var_385, var_386, var_387});
        var_389 = transform_twist_1(var_X_sc, var_388);
        var_391 = wp::add(var_qd_start, var_390);
        wp::array_store(var_joint_S_s, var_391, var_389);
        // return v_j_s, wp.spatial_vector()                                                  <L 494>
        var_392 = wp::vec_t<6, wp::float32>();
        ret_0 = var_331;
        ret_1 = var_392;
        return;
    }
    var_393 = wp::where(var_301, var_331, var_243);
    // wp.printf("jcalc_motion not implemented for joint type %d\n", type)                    <L 496>
    printf(var_394, var_type);
    // return wp.spatial_vector(), wp.spatial_vector()                                        <L 499>
    var_395 = wp::vec_t<6, wp::float32>();
    var_396 = wp::vec_t<6, wp::float32>();
    ret_0 = var_395;
    ret_1 = var_396;
    return;
}


// newton/_src/solvers/featherstone/kernels.py:851
static wp::vec_t<6, wp::float32> spatial_cross_0(
    wp::vec_t<6, wp::float32> var_a,
    wp::vec_t<6, wp::float32> var_b)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    wp::vec_t<3, wp::float32> var_4{};
    wp::vec_t<3, wp::float32> var_5{};
    wp::vec_t<3, wp::float32> var_6{};
    wp::vec_t<3, wp::float32> var_7{};
    wp::vec_t<6, wp::float32> var_8{};
    //---------
    // forward
    // def spatial_cross(a: wp.spatial_vector, b: wp.spatial_vector):                         <L 852>
    // w_a = wp.spatial_bottom(a)                                                             <L 853>
    var_0 = wp::spatial_bottom(var_a);
    // v_a = wp.spatial_top(a)                                                                <L 854>
    var_1 = wp::spatial_top(var_a);
    // w_b = wp.spatial_bottom(b)                                                             <L 856>
    var_2 = wp::spatial_bottom(var_b);
    // v_b = wp.spatial_top(b)                                                                <L 857>
    var_3 = wp::spatial_top(var_b);
    // w = wp.cross(w_a, w_b)                                                                 <L 859>
    var_4 = wp::cross(var_0, var_2);
    // v = wp.cross(w_a, v_b) + wp.cross(v_a, w_b)                                            <L 860>
    var_5 = wp::cross(var_0, var_3);
    var_6 = wp::cross(var_1, var_2);
    var_7 = wp::add(var_5, var_6);
    // return wp.spatial_vector(v, w)                                                         <L 862>
    var_8 = wp::vec_t<6, wp::float32>(var_7, var_4);
    return var_8;
}


// newton/_src/solvers/featherstone/kernels.py:186
static wp::mat_t<6, 6, wp::float32> transform_spatial_inertia_1(
    wp::transform_t<wp::float32> var_t,
    wp::mat_t<6, 6, wp::float32> var_I)
{
    //---------
    // primal vars
    wp::transform_t<wp::float32> var_0{};
    wp::quat_t<wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    const wp::float32 var_3 = 1.0;
    const wp::float32 var_4 = 0.0;
    const wp::float32 var_5 = 0.0;
    wp::vec_t<3, wp::float32> var_6{};
    wp::vec_t<3, wp::float32> var_7{};
    const wp::float32 var_8 = 0.0;
    const wp::float32 var_9 = 1.0;
    const wp::float32 var_10 = 0.0;
    wp::vec_t<3, wp::float32> var_11{};
    wp::vec_t<3, wp::float32> var_12{};
    const wp::float32 var_13 = 0.0;
    const wp::float32 var_14 = 0.0;
    const wp::float32 var_15 = 1.0;
    wp::vec_t<3, wp::float32> var_16{};
    wp::vec_t<3, wp::float32> var_17{};
    wp::mat_t<3, 3, wp::float32> var_18{};
    wp::mat_t<3, 3, wp::float32> var_19{};
    wp::mat_t<3, 3, wp::float32> var_20{};
    const wp::int32 var_21 = 0;
    const wp::int32 var_22 = 0;
    wp::float32 var_23{};
    const wp::int32 var_24 = 0;
    const wp::int32 var_25 = 1;
    wp::float32 var_26{};
    const wp::int32 var_27 = 0;
    const wp::int32 var_28 = 2;
    wp::float32 var_29{};
    const wp::int32 var_30 = 0;
    const wp::int32 var_31 = 0;
    wp::float32 var_32{};
    const wp::int32 var_33 = 0;
    const wp::int32 var_34 = 1;
    wp::float32 var_35{};
    const wp::int32 var_36 = 0;
    const wp::int32 var_37 = 2;
    wp::float32 var_38{};
    const wp::int32 var_39 = 1;
    const wp::int32 var_40 = 0;
    wp::float32 var_41{};
    const wp::int32 var_42 = 1;
    const wp::int32 var_43 = 1;
    wp::float32 var_44{};
    const wp::int32 var_45 = 1;
    const wp::int32 var_46 = 2;
    wp::float32 var_47{};
    const wp::int32 var_48 = 1;
    const wp::int32 var_49 = 0;
    wp::float32 var_50{};
    const wp::int32 var_51 = 1;
    const wp::int32 var_52 = 1;
    wp::float32 var_53{};
    const wp::int32 var_54 = 1;
    const wp::int32 var_55 = 2;
    wp::float32 var_56{};
    const wp::int32 var_57 = 2;
    const wp::int32 var_58 = 0;
    wp::float32 var_59{};
    const wp::int32 var_60 = 2;
    const wp::int32 var_61 = 1;
    wp::float32 var_62{};
    const wp::int32 var_63 = 2;
    const wp::int32 var_64 = 2;
    wp::float32 var_65{};
    const wp::int32 var_66 = 2;
    const wp::int32 var_67 = 0;
    wp::float32 var_68{};
    const wp::int32 var_69 = 2;
    const wp::int32 var_70 = 1;
    wp::float32 var_71{};
    const wp::int32 var_72 = 2;
    const wp::int32 var_73 = 2;
    wp::float32 var_74{};
    const wp::float32 var_75 = 0.0;
    const wp::float32 var_76 = 0.0;
    const wp::float32 var_77 = 0.0;
    const wp::int32 var_78 = 0;
    const wp::int32 var_79 = 0;
    wp::float32 var_80{};
    const wp::int32 var_81 = 0;
    const wp::int32 var_82 = 1;
    wp::float32 var_83{};
    const wp::int32 var_84 = 0;
    const wp::int32 var_85 = 2;
    wp::float32 var_86{};
    const wp::float32 var_87 = 0.0;
    const wp::float32 var_88 = 0.0;
    const wp::float32 var_89 = 0.0;
    const wp::int32 var_90 = 1;
    const wp::int32 var_91 = 0;
    wp::float32 var_92{};
    const wp::int32 var_93 = 1;
    const wp::int32 var_94 = 1;
    wp::float32 var_95{};
    const wp::int32 var_96 = 1;
    const wp::int32 var_97 = 2;
    wp::float32 var_98{};
    const wp::float32 var_99 = 0.0;
    const wp::float32 var_100 = 0.0;
    const wp::float32 var_101 = 0.0;
    const wp::int32 var_102 = 2;
    const wp::int32 var_103 = 0;
    wp::float32 var_104{};
    const wp::int32 var_105 = 2;
    const wp::int32 var_106 = 1;
    wp::float32 var_107{};
    const wp::int32 var_108 = 2;
    const wp::int32 var_109 = 2;
    wp::float32 var_110{};
    wp::mat_t<6, 6, wp::float32> var_111{};
    wp::mat_t<6, 6, wp::float32> var_112{};
    wp::mat_t<6, 6, wp::float32> var_113{};
    wp::mat_t<6, 6, wp::float32> var_114{};
    //---------
    // forward
    // def transform_spatial_inertia(t: wp.transform, I: wp.spatial_matrix):                  <L 187>
    // t_inv = wp.transform_inverse(t)                                                        <L 207>
    var_0 = wp::transform_inverse(var_t);
    // q = wp.transform_get_rotation(t_inv)                                                   <L 209>
    var_1 = wp::transform_get_rotation(var_0);
    // p = wp.transform_get_translation(t_inv)                                                <L 210>
    var_2 = wp::transform_get_translation(var_0);
    // r1 = wp.quat_rotate(q, wp.vec3(1.0, 0.0, 0.0))                                         <L 212>
    var_6 = wp::vec_t<3, wp::float32>(var_3, var_4, var_5);
    var_7 = wp::quat_rotate(var_1, var_6);
    // r2 = wp.quat_rotate(q, wp.vec3(0.0, 1.0, 0.0))                                         <L 213>
    var_11 = wp::vec_t<3, wp::float32>(var_8, var_9, var_10);
    var_12 = wp::quat_rotate(var_1, var_11);
    // r3 = wp.quat_rotate(q, wp.vec3(0.0, 0.0, 1.0))                                         <L 214>
    var_16 = wp::vec_t<3, wp::float32>(var_13, var_14, var_15);
    var_17 = wp::quat_rotate(var_1, var_16);
    // R = wp.matrix_from_cols(r1, r2, r3)                                                    <L 216>
    var_18 = wp::matrix_from_cols<wp::float32>(var_7, var_12, var_17);
    // S = wp.skew(p) @ R                                                                     <L 217>
    var_19 = wp::skew(var_2);
    var_20 = wp::mul(var_19, var_18);
    // T = wp.spatial_matrix(                                                                 <L 219>
    // R[0, 0],                                                                               <L 220>
    var_23 = wp::extract(var_18, var_21, var_22);
    // R[0, 1],                                                                               <L 221>
    var_26 = wp::extract(var_18, var_24, var_25);
    // R[0, 2],                                                                               <L 222>
    var_29 = wp::extract(var_18, var_27, var_28);
    // S[0, 0],                                                                               <L 223>
    var_32 = wp::extract(var_20, var_30, var_31);
    // S[0, 1],                                                                               <L 224>
    var_35 = wp::extract(var_20, var_33, var_34);
    // S[0, 2],                                                                               <L 225>
    var_38 = wp::extract(var_20, var_36, var_37);
    // R[1, 0],                                                                               <L 226>
    var_41 = wp::extract(var_18, var_39, var_40);
    // R[1, 1],                                                                               <L 227>
    var_44 = wp::extract(var_18, var_42, var_43);
    // R[1, 2],                                                                               <L 228>
    var_47 = wp::extract(var_18, var_45, var_46);
    // S[1, 0],                                                                               <L 229>
    var_50 = wp::extract(var_20, var_48, var_49);
    // S[1, 1],                                                                               <L 230>
    var_53 = wp::extract(var_20, var_51, var_52);
    // S[1, 2],                                                                               <L 231>
    var_56 = wp::extract(var_20, var_54, var_55);
    // R[2, 0],                                                                               <L 232>
    var_59 = wp::extract(var_18, var_57, var_58);
    // R[2, 1],                                                                               <L 233>
    var_62 = wp::extract(var_18, var_60, var_61);
    // R[2, 2],                                                                               <L 234>
    var_65 = wp::extract(var_18, var_63, var_64);
    // S[2, 0],                                                                               <L 235>
    var_68 = wp::extract(var_20, var_66, var_67);
    // S[2, 1],                                                                               <L 236>
    var_71 = wp::extract(var_20, var_69, var_70);
    // S[2, 2],                                                                               <L 237>
    var_74 = wp::extract(var_20, var_72, var_73);
    // 0.0,                                                                                   <L 238>
    // 0.0,                                                                                   <L 239>
    // 0.0,                                                                                   <L 240>
    // R[0, 0],                                                                               <L 241>
    var_80 = wp::extract(var_18, var_78, var_79);
    // R[0, 1],                                                                               <L 242>
    var_83 = wp::extract(var_18, var_81, var_82);
    // R[0, 2],                                                                               <L 243>
    var_86 = wp::extract(var_18, var_84, var_85);
    // 0.0,                                                                                   <L 244>
    // 0.0,                                                                                   <L 245>
    // 0.0,                                                                                   <L 246>
    // R[1, 0],                                                                               <L 247>
    var_92 = wp::extract(var_18, var_90, var_91);
    // R[1, 1],                                                                               <L 248>
    var_95 = wp::extract(var_18, var_93, var_94);
    // R[1, 2],                                                                               <L 249>
    var_98 = wp::extract(var_18, var_96, var_97);
    // 0.0,                                                                                   <L 250>
    // 0.0,                                                                                   <L 251>
    // 0.0,                                                                                   <L 252>
    // R[2, 0],                                                                               <L 253>
    var_104 = wp::extract(var_18, var_102, var_103);
    // R[2, 1],                                                                               <L 254>
    var_107 = wp::extract(var_18, var_105, var_106);
    // R[2, 2],                                                                               <L 255>
    var_110 = wp::extract(var_18, var_108, var_109);
    var_111 = wp::mat_t<6, 6, wp::float32>({var_23, var_26, var_29, var_32, var_35, var_38, var_41, var_44, var_47, var_50, var_53, var_56, var_59, var_62, var_65, var_68, var_71, var_74, var_75, var_76, var_77, var_80, var_83, var_86, var_87, var_88, var_89, var_92, var_95, var_98, var_99, var_100, var_101, var_104, var_107, var_110});
    // return wp.mul(wp.mul(wp.transpose(T), I), T)                                           <L 258>
    var_112 = wp::transpose(var_111);
    var_113 = wp::mul(var_112, var_I);
    var_114 = wp::mul(var_113, var_111);
    return var_114;
}


// newton/_src/solvers/featherstone/kernels.py:865
static wp::vec_t<6, wp::float32> spatial_cross_dual_0(
    wp::vec_t<6, wp::float32> var_a,
    wp::vec_t<6, wp::float32> var_b)
{
    //---------
    // primal vars
    wp::vec_t<3, wp::float32> var_0{};
    wp::vec_t<3, wp::float32> var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    wp::vec_t<3, wp::float32> var_4{};
    wp::vec_t<3, wp::float32> var_5{};
    wp::vec_t<3, wp::float32> var_6{};
    wp::vec_t<3, wp::float32> var_7{};
    wp::vec_t<6, wp::float32> var_8{};
    //---------
    // forward
    // def spatial_cross_dual(a: wp.spatial_vector, b: wp.spatial_vector):                    <L 866>
    // w_a = wp.spatial_bottom(a)                                                             <L 867>
    var_0 = wp::spatial_bottom(var_a);
    // v_a = wp.spatial_top(a)                                                                <L 868>
    var_1 = wp::spatial_top(var_a);
    // w_b = wp.spatial_bottom(b)                                                             <L 870>
    var_2 = wp::spatial_bottom(var_b);
    // v_b = wp.spatial_top(b)                                                                <L 871>
    var_3 = wp::spatial_top(var_b);
    // w = wp.cross(w_a, w_b) + wp.cross(v_a, v_b)                                            <L 873>
    var_4 = wp::cross(var_0, var_2);
    var_5 = wp::cross(var_1, var_3);
    var_6 = wp::add(var_4, var_5);
    // v = wp.cross(w_a, v_b)                                                                 <L 874>
    var_7 = wp::cross(var_0, var_3);
    // return wp.spatial_vector(v, w)                                                         <L 876>
    var_8 = wp::vec_t<6, wp::float32>(var_7, var_6);
    return var_8;
}


// newton/_src/solvers/featherstone/kernels.py:884
static void compute_link_velocity_0(
    wp::int32 var_i,
    wp::vec_t<3, wp::float32> var_solve_origin,
    wp::array_t<wp::int32> var_joint_type,
    wp::array_t<wp::int32> var_joint_parent,
    wp::array_t<wp::int32> var_joint_child,
    wp::array_t<wp::int32> var_joint_q_start,
    wp::array_t<wp::int32> var_joint_qd_start,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::float32> var_joint_qdd,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::int32> var_joint_dof_dim,
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_m,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p,
    wp::array_t<wp::int32> var_body_world,
    wp::array_t<wp::vec_t<3, wp::float32>> var_gravity,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd,
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s,
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_solve_origin,
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_s,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_v_s,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_s,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_a_s)
{
    //---------
    // primal vars
    wp::int32* var_0{};
    wp::int32 var_1{};
    wp::int32 var_2{};
    wp::int32* var_3{};
    wp::int32 var_4{};
    wp::int32 var_5{};
    wp::int32* var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32* var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    wp::int32* var_12{};
    wp::int32 var_13{};
    wp::int32 var_14{};
    wp::transform_t<wp::float32>* var_15{};
    wp::transform_t<wp::float32> var_16{};
    wp::transform_t<wp::float32> var_17{};
    wp::transform_t<wp::float32> var_18{};
    const wp::int32 var_19 = 0;
    bool var_20{};
    wp::transform_t<wp::float32>* var_21{};
    wp::transform_t<wp::float32> var_22{};
    wp::transform_t<wp::float32> var_23{};
    wp::transform_t<wp::float32> var_24{};
    wp::transform_t<wp::float32> var_25{};
    wp::vec_t<3, wp::float32> var_26{};
    wp::vec_t<3, wp::float32> var_27{};
    wp::quat_t<wp::float32> var_28{};
    wp::transform_t<wp::float32> var_29{};
    const wp::int32 var_30 = 0;
    wp::int32* var_31{};
    wp::int32 var_32{};
    wp::int32 var_33{};
    const wp::int32 var_34 = 1;
    wp::int32* var_35{};
    wp::int32 var_36{};
    wp::int32 var_37{};
    wp::vec_t<6, wp::float32> var_38{};
    wp::vec_t<6, wp::float32> var_39{};
    wp::vec_t<6, wp::float32> var_40{};
    wp::vec_t<6, wp::float32> var_41{};
    const wp::int32 var_42 = 0;
    bool var_43{};
    wp::vec_t<6, wp::float32>* var_44{};
    wp::vec_t<6, wp::float32> var_45{};
    wp::vec_t<6, wp::float32> var_46{};
    wp::vec_t<6, wp::float32>* var_47{};
    wp::vec_t<6, wp::float32> var_48{};
    wp::vec_t<6, wp::float32> var_49{};
    wp::vec_t<6, wp::float32> var_50{};
    wp::vec_t<6, wp::float32> var_51{};
    wp::vec_t<6, wp::float32> var_52{};
    wp::vec_t<6, wp::float32> var_53{};
    wp::vec_t<6, wp::float32> var_54{};
    wp::vec_t<6, wp::float32> var_55{};
    const wp::int32 var_56 = 1;
    wp::int32 var_57{};
    wp::int32* var_58{};
    wp::range_t var_59{};
    wp::int32 var_60{};
    wp::int32 var_61{};
    wp::vec_t<6, wp::float32>* var_62{};
    wp::float32* var_63{};
    wp::vec_t<6, wp::float32> var_64{};
    wp::vec_t<6, wp::float32> var_65{};
    wp::float32 var_66{};
    wp::vec_t<6, wp::float32> var_67{};
    wp::transform_t<wp::float32>* var_68{};
    wp::transform_t<wp::float32> var_69{};
    wp::transform_t<wp::float32> var_70{};
    wp::vec_t<3, wp::float32> var_71{};
    wp::vec_t<3, wp::float32> var_72{};
    wp::mat_t<6, 6, wp::float32>* var_73{};
    wp::mat_t<6, 6, wp::float32> var_74{};
    wp::mat_t<6, 6, wp::float32> var_75{};
    const wp::int32 var_76 = 0;
    const wp::int32 var_77 = 0;
    wp::float32 var_78{};
    wp::int32* var_79{};
    wp::int32 var_80{};
    wp::int32 var_81{};
    wp::vec_t<3, wp::float32>* var_82{};
    wp::vec_t<3, wp::float32> var_83{};
    wp::vec_t<3, wp::float32> var_84{};
    wp::vec_t<3, wp::float32> var_85{};
    wp::vec_t<3, wp::float32> var_86{};
    wp::vec_t<6, wp::float32> var_87{};
    wp::quat_t<wp::float32> var_88{};
    wp::transform_t<wp::float32> var_89{};
    wp::mat_t<6, 6, wp::float32> var_90{};
    wp::vec_t<6, wp::float32> var_91{};
    wp::vec_t<6, wp::float32> var_92{};
    wp::vec_t<6, wp::float32> var_93{};
    wp::vec_t<6, wp::float32> var_94{};
    wp::vec_t<3, wp::float32> var_95{};
    wp::vec_t<3, wp::float32> var_96{};
    wp::vec_t<3, wp::float32> var_97{};
    wp::vec_t<3, wp::float32> var_98{};
    wp::vec_t<6, wp::float32> var_99{};
    wp::vec_t<6, wp::float32> var_100{};
    //---------
    // forward
    // def compute_link_velocity(                                                             <L 885>
    // type = joint_type[i]                                                                   <L 913>
    var_0 = wp::address(var_joint_type, var_i);
    var_2 = wp::load(var_0);
    var_1 = wp::copy(var_2);
    // child = joint_child[i]                                                                 <L 914>
    var_3 = wp::address(var_joint_child, var_i);
    var_5 = wp::load(var_3);
    var_4 = wp::copy(var_5);
    // parent = joint_parent[i]                                                               <L 915>
    var_6 = wp::address(var_joint_parent, var_i);
    var_8 = wp::load(var_6);
    var_7 = wp::copy(var_8);
    // q_start = joint_q_start[i]                                                             <L 916>
    var_9 = wp::address(var_joint_q_start, var_i);
    var_11 = wp::load(var_9);
    var_10 = wp::copy(var_11);
    // qd_start = joint_qd_start[i]                                                           <L 917>
    var_12 = wp::address(var_joint_qd_start, var_i);
    var_14 = wp::load(var_12);
    var_13 = wp::copy(var_14);
    // X_pj = joint_X_p[i]                                                                    <L 919>
    var_15 = wp::address(var_joint_X_p, var_i);
    var_17 = wp::load(var_15);
    var_16 = wp::copy(var_17);
    // X_wpj = X_pj                                                                           <L 923>
    var_18 = wp::copy(var_16);
    // if parent >= 0:                                                                        <L 924>
    var_20 = (var_7 >= var_19);
    if (var_20) {
        // X_wp = body_q[parent]                                                              <L 925>
        var_21 = wp::address(var_body_q, var_7);
        var_23 = wp::load(var_21);
        var_22 = wp::copy(var_23);
        // X_wpj = X_wp * X_wpj                                                               <L 926>
        var_24 = wp::mul(var_22, var_18);
    }
    var_25 = wp::where(var_20, var_24, var_18);
    // X_wpj_s = wp.transform(wp.transform_get_translation(X_wpj) - solve_origin, wp.transform_get_rotation(X_wpj))       <L 927>
    var_26 = wp::transform_get_translation(var_25);
    var_27 = wp::sub(var_26, var_solve_origin);
    var_28 = wp::transform_get_rotation(var_25);
    var_29 = wp::transform_t<wp::float32>(var_27, var_28);
    // lin_axis_count = joint_dof_dim[i, 0]                                                   <L 930>
    var_31 = wp::address(var_joint_dof_dim, var_i, var_30);
    var_33 = wp::load(var_31);
    var_32 = wp::copy(var_33);
    // ang_axis_count = joint_dof_dim[i, 1]                                                   <L 931>
    var_35 = wp::address(var_joint_dof_dim, var_i, var_34);
    var_37 = wp::load(var_35);
    var_36 = wp::copy(var_37);
    // v_j_s, c_app_s = jcalc_motion(                                                         <L 932>
    // type,                                                                                  <L 933>
    // joint_axis,                                                                            <L 934>
    // joint_q,                                                                               <L 935>
    // lin_axis_count,                                                                        <L 936>
    // ang_axis_count,                                                                        <L 937>
    // X_wpj_s,                                                                               <L 938>
    // joint_qd,                                                                              <L 939>
    // q_start,                                                                               <L 940>
    // qd_start,                                                                              <L 941>
    // joint_S_s,                                                                             <L 942>
    jcalc_motion_0(var_1, var_joint_axis, var_joint_q, var_32, var_36, var_29, var_joint_qd, var_10, var_13, var_joint_S_s, var_38, var_39);
    // v_parent_s = wp.spatial_vector()                                                       <L 946>
    var_40 = wp::vec_t<6, wp::float32>();
    // a_parent_s = wp.spatial_vector()                                                       <L 947>
    var_41 = wp::vec_t<6, wp::float32>();
    // if parent >= 0:                                                                        <L 949>
    var_43 = (var_7 >= var_42);
    if (var_43) {
        // v_parent_s = body_v_s[parent]                                                      <L 950>
        var_44 = wp::address(var_body_v_s, var_7);
        var_46 = wp::load(var_44);
        var_45 = wp::copy(var_46);
        // a_parent_s = body_a_s[parent]                                                      <L 951>
        var_47 = wp::address(var_body_a_s, var_7);
        var_49 = wp::load(var_47);
        var_48 = wp::copy(var_49);
    }
    var_50 = wp::where(var_43, var_45, var_40);
    var_51 = wp::where(var_43, var_48, var_41);
    // v_s = v_parent_s + v_j_s                                                               <L 954>
    var_52 = wp::add(var_50, var_38);
    // a_s = a_parent_s + spatial_cross(v_s, v_j_s) + c_app_s                                 <L 958>
    var_53 = spatial_cross_0(var_52, var_38);
    var_54 = wp::add(var_51, var_53);
    var_55 = wp::add(var_54, var_39);
    // if joint_qdd:                                                                          <L 959>
    if (var_joint_qdd) {
        // for j in range(qd_start, joint_qd_start[i + 1]):                                   <L 960>
        var_57 = wp::add(var_i, var_56);
        var_58 = wp::address(var_joint_qd_start, var_57);
        var_60 = wp::load(var_58);
        var_59 = wp::range(var_13, var_60);
        start_for_0:;
            if (iter_cmp(var_59) == 0) goto end_for_0;
            var_61 = wp::iter_next(var_59);
            // a_s = a_s + joint_S_s[j] * joint_qdd[j]                                        <L 961>
            var_62 = wp::address(var_joint_S_s, var_61);
            var_63 = wp::address(var_joint_qdd, var_61);
            var_65 = wp::load(var_62);
            var_66 = wp::load(var_63);
            var_64 = wp::mul(var_65, var_66);
            var_67 = wp::add(var_55, var_64);
            wp::assign(var_55, var_67);
            goto start_for_0;
        end_for_0:;
    }
    // X_sm = body_q_com[child]                                                               <L 964>
    var_68 = wp::address(var_body_q_com, var_4);
    var_70 = wp::load(var_68);
    var_69 = wp::copy(var_70);
    // x_com_s = wp.transform_get_translation(X_sm) - solve_origin                            <L 965>
    var_71 = wp::transform_get_translation(var_69);
    var_72 = wp::sub(var_71, var_solve_origin);
    // body_solve_origin[child] = solve_origin                                                <L 966>
    wp::array_store(var_body_solve_origin, var_4, var_solve_origin);
    // I_m = body_I_m[child]                                                                  <L 967>
    var_73 = wp::address(var_body_I_m, var_4);
    var_75 = wp::load(var_73);
    var_74 = wp::copy(var_75);
    // m = I_m[0, 0]                                                                          <L 970>
    var_78 = wp::extract(var_74, var_76, var_77);
    // world_idx = body_world[child]                                                          <L 972>
    var_79 = wp::address(var_body_world, var_4);
    var_81 = wp::load(var_79);
    var_80 = wp::copy(var_81);
    // world_g = gravity[world_idx]                                                           <L 973>
    var_82 = wp::address(var_gravity, var_80);
    var_84 = wp::load(var_82);
    var_83 = wp::copy(var_84);
    // f_g = m * world_g                                                                      <L 974>
    var_85 = wp::mul(var_78, var_83);
    // f_g_s = wp.spatial_vector(f_g, wp.cross(x_com_s, f_g))                                 <L 975>
    var_86 = wp::cross(var_72, var_85);
    var_87 = wp::vec_t<6, wp::float32>(var_85, var_86);
    // X_sm_s = wp.transform(x_com_s, wp.transform_get_rotation(X_sm))                        <L 978>
    var_88 = wp::transform_get_rotation(var_69);
    var_89 = wp::transform_t<wp::float32>(var_72, var_88);
    // I_s = transform_spatial_inertia(X_sm_s, I_m)                                           <L 979>
    var_90 = transform_spatial_inertia_1(var_89, var_74);
    // f_b_s = I_s * a_s + spatial_cross_dual(v_s, I_s * v_s)                                 <L 981>
    var_91 = wp::mul(var_90, var_55);
    var_92 = wp::mul(var_90, var_52);
    var_93 = spatial_cross_dual_0(var_52, var_92);
    var_94 = wp::add(var_91, var_93);
    // omega_world = wp.spatial_bottom(v_s)                                                   <L 982>
    var_95 = wp::spatial_bottom(var_52);
    // v_com_world = wp.spatial_top(v_s) + wp.cross(omega_world, x_com_s)                     <L 983>
    var_96 = wp::spatial_top(var_52);
    var_97 = wp::cross(var_95, var_72);
    var_98 = wp::add(var_96, var_97);
    // body_qd[child] = wp.spatial_vector(v_com_world, omega_world)                           <L 985>
    var_99 = wp::vec_t<6, wp::float32>(var_98, var_95);
    wp::array_store(var_body_qd, var_4, var_99);
    // body_v_s[child] = v_s                                                                  <L 986>
    wp::array_store(var_body_v_s, var_4, var_52);
    // body_a_s[child] = a_s                                                                  <L 987>
    wp::array_store(var_body_a_s, var_4, var_55);
    // body_f_s[child] = f_b_s - f_g_s                                                        <L 988>
    var_100 = wp::sub(var_94, var_87);
    wp::array_store(var_body_f_s, var_4, var_100);
    // body_I_s[child] = I_s                                                                  <L 989>
    wp::array_store(var_body_I_s, var_4, var_90);
}


// newton/_src/solvers/featherstone/kernels.py:1809
static void adj_dense_solve_0(
    wp::int32 var_n,
    wp::int32 var_L_start,
    wp::int32 var_b_start,
    wp::array_t<wp::float32> var_A,
    wp::array_t<wp::float32> var_L,
    wp::array_t<wp::float32> var_b,
    wp::array_t<wp::float32> var_x,
    wp::array_t<wp::float32> var_tmp,
    wp::int32 & adj_n,
    wp::int32 & adj_L_start,
    wp::int32 & adj_b_start,
    wp::array_t<wp::float32> & adj_A,
    wp::array_t<wp::float32> & adj_L,
    wp::array_t<wp::float32> & adj_b,
    wp::array_t<wp::float32> & adj_x,
    wp::array_t<wp::float32> & adj_tmp)
{
	// user-defined adjoint code
    //---------
    // primal vars
    bool var_0{};
    bool var_1{};
    bool var_2{};
    bool var_3{};
    bool var_4{};
    wp::range_t var_5{};
    wp::int32 var_6{};
    const wp::float32 var_7 = 0.0;
    wp::int32 var_8{};
    wp::range_t var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    wp::float32* var_13{};
    wp::float32 var_14{};
    wp::float32 var_15{};
    wp::range_t var_16{};
    wp::int32 var_17{};
    wp::range_t var_18{};
    wp::int32 var_19{};
    wp::int32 var_20{};
    wp::int32 var_21{};
    wp::int32 var_22{};
    wp::float32* var_23{};
    wp::float32 var_24{};
    wp::float32 var_25{};
    wp::int32 var_26{};
    wp::float32* var_27{};
    wp::float32 var_28{};
    wp::float32 var_29{};
    wp::float32 var_30{};
    wp::range_t var_31{};
    wp::int32 var_32{};
    wp::range_t var_33{};
    wp::int32 var_34{};
    wp::int32 var_35{};
    wp::int32 var_36{};
    wp::int32 var_37{};
    wp::float32* var_38{};
    wp::float32 var_39{};
    wp::float32 var_40{};
    wp::int32 var_41{};
    wp::float32* var_42{};
    wp::float32 var_43{};
    wp::float32 var_44{};
    wp::float32 var_45{};
    //---------
    // forward
    // def adj_dense_solve(                                                                   <L 1810>
    // if not tmp or not wp.adjoint[x] or not wp.adjoint[A] or not wp.adjoint[L]:             <L 1821>
    var_1 = wp::unot(var_tmp);
    var_0 = var_1;
    if (!var_0) {
        var_2 = wp::unot(adj_x);
        var_0 = var_0 || var_2;
    }
    if (!var_0) {
        var_3 = wp::unot(adj_A);
        var_0 = var_0 || var_3;
    }
    if (!var_0) {
        var_4 = wp::unot(adj_L);
        var_0 = var_0 || var_4;
    }
    if (var_0) {
        // return                                                                             <L 1822>
        return;
    }
    // for i in range(n):                                                                     <L 1823>
    var_5 = wp::range(var_n);
    start_for_1:;
        if (iter_cmp(var_5) == 0) goto end_for_1;
        var_6 = wp::iter_next(var_5);
        // tmp[b_start + i] = 0.0                                                             <L 1824>
        var_8 = wp::add(var_b_start, var_6);
        wp::array_store(var_tmp, var_8, var_7);
        goto start_for_1;
    end_for_1:;
    // dense_subs(n, L_start, b_start, L, wp.adjoint[x], tmp)                                 <L 1826>
    dense_subs_0(var_n, var_L_start, var_b_start, var_L, adj_x, var_tmp);
    // for i in range(n):                                                                     <L 1828>
    var_9 = wp::range(var_n);
    start_for_3:;
        if (iter_cmp(var_9) == 0) goto end_for_3;
        var_10 = wp::iter_next(var_9);
        // wp.adjoint[b][b_start + i] += tmp[b_start + i]                                     <L 1829>
        var_11 = wp::add(var_b_start, var_10);
        var_12 = wp::add(var_b_start, var_10);
        var_13 = wp::address(var_tmp, var_12);
        var_15 = wp::load(var_13);
        var_14 = wp::atomic_add(adj_b, var_11, var_15);
        goto start_for_3;
    end_for_3:;
    // for i in range(n):                                                                     <L 1832>
    var_16 = wp::range(var_n);
    start_for_5:;
        if (iter_cmp(var_16) == 0) goto end_for_5;
        var_17 = wp::iter_next(var_16);
        // for j in range(n):                                                                 <L 1833>
        var_18 = wp::range(var_n);
        start_for_7:;
            if (iter_cmp(var_18) == 0) goto end_for_7;
            var_19 = wp::iter_next(var_18);
            // wp.adjoint[L][L_start + dense_index(n, i, j)] += -tmp[b_start + i] * x[b_start + j]       <L 1834>
            var_20 = dense_index_0(var_n, var_17, var_19);
            var_21 = wp::add(var_L_start, var_20);
            var_22 = wp::add(var_b_start, var_17);
            var_23 = wp::address(var_tmp, var_22);
            var_25 = wp::load(var_23);
            var_24 = wp::neg(var_25);
            var_26 = wp::add(var_b_start, var_19);
            var_27 = wp::address(var_x, var_26);
            var_29 = wp::load(var_27);
            var_28 = wp::mul(var_24, var_29);
            var_30 = wp::atomic_add(adj_L, var_21, var_28);
            goto start_for_7;
        end_for_7:;
        goto start_for_5;
    end_for_5:;
    // for i in range(n):                                                                     <L 1836>
    var_31 = wp::range(var_n);
    start_for_9:;
        if (iter_cmp(var_31) == 0) goto end_for_9;
        var_32 = wp::iter_next(var_31);
        // for j in range(n):                                                                 <L 1837>
        var_33 = wp::range(var_n);
        start_for_11:;
            if (iter_cmp(var_33) == 0) goto end_for_11;
            var_34 = wp::iter_next(var_33);
            // wp.adjoint[A][L_start + dense_index(n, i, j)] += -tmp[b_start + i] * x[b_start + j]       <L 1838>
            var_35 = dense_index_0(var_n, var_32, var_34);
            var_36 = wp::add(var_L_start, var_35);
            var_37 = wp::add(var_b_start, var_32);
            var_38 = wp::address(var_tmp, var_37);
            var_40 = wp::load(var_38);
            var_39 = wp::neg(var_40);
            var_41 = wp::add(var_b_start, var_34);
            var_42 = wp::address(var_x, var_41);
            var_44 = wp::load(var_42);
            var_43 = wp::mul(var_39, var_44);
            var_45 = wp::atomic_add(adj_A, var_36, var_43);
            goto start_for_11;
        end_for_11:;
        wp::assign(var_19, var_34);
        goto start_for_9;
    end_for_9:;
}


// newton/_src/solvers/featherstone/kernels.py:1731
static void adj_dense_cholesky_0(
    wp::int32 var_n,
    wp::array_t<wp::float32> var_A,
    wp::array_t<wp::float32> var_R,
    wp::int32 var_A_start,
    wp::int32 var_R_start,
    wp::array_t<wp::float32> var_L,
    wp::int32 & adj_n,
    wp::array_t<wp::float32> & adj_A,
    wp::array_t<wp::float32> & adj_R,
    wp::int32 & adj_A_start,
    wp::int32 & adj_R_start,
    wp::array_t<wp::float32> & adj_L)
{
	// user-defined adjoint code
    //---------
    // primal vars
    //---------
    // forward
    // def adj_dense_cholesky(                                                                <L 1732>
    // pass                                                                                   <L 1742>
}


// newton/_src/solvers/featherstone/kernels.py:584
static void adj_jcalc_integrate_0(
    wp::int32 var_parent,
    wp::transform_t<wp::float32> var_joint_X_c,
    wp::vec_t<3, wp::float32> var_body_com_child,
    wp::int32 var_type,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::float32> var_joint_qdd,
    wp::int32 var_coord_start,
    wp::int32 var_dof_start,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::float32 var_dt,
    wp::array_t<wp::float32> var_joint_q_new,
    wp::array_t<wp::float32> var_joint_qd_new,
    wp::int32 & adj_parent,
    wp::transform_t<wp::float32> & adj_joint_X_c,
    wp::vec_t<3, wp::float32> & adj_body_com_child,
    wp::int32 & adj_type,
    wp::array_t<wp::float32> & adj_joint_q,
    wp::array_t<wp::float32> & adj_joint_qd,
    wp::array_t<wp::float32> & adj_joint_qdd,
    wp::int32 & adj_coord_start,
    wp::int32 & adj_dof_start,
    wp::int32 & adj_lin_axis_count,
    wp::int32 & adj_ang_axis_count,
    wp::float32 & adj_dt,
    wp::array_t<wp::float32> & adj_joint_q_new,
    wp::array_t<wp::float32> & adj_joint_qd_new)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:879
static void adj_dense_index_0(
    wp::int32 var_stride,
    wp::int32 var_i,
    wp::int32 var_j,
    wp::int32 & adj_stride,
    wp::int32 & adj_i,
    wp::int32 & adj_j,
    wp::int32 & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:1763
static void adj_dense_subs_0(
    wp::int32 var_n,
    wp::int32 var_L_start,
    wp::int32 var_b_start,
    wp::array_t<wp::float32> var_L,
    wp::array_t<wp::float32> var_b,
    wp::array_t<wp::float32> var_x,
    wp::int32 & adj_n,
    wp::int32 & adj_L_start,
    wp::int32 & adj_b_start,
    wp::array_t<wp::float32> & adj_L,
    wp::array_t<wp::float32> & adj_b,
    wp::array_t<wp::float32> & adj_x)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/sim/articulation.py:39
static void adj_transform_2d_rotational_axes_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::float32 var_q0,
    wp::vec_t<3, wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1,
    wp::vec_t<3, wp::float32> & adj_axis_0,
    wp::vec_t<3, wp::float32> & adj_axis_1,
    wp::float32 & adj_q0,
    wp::vec_t<3, wp::float32> & adj_ret_0,
    wp::vec_t<3, wp::float32> & adj_ret_1)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/sim/articulation.py:64
static void adj_compute_2d_rotational_dofs_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::float32 var_q0,
    wp::float32 var_q1,
    wp::float32 var_qd0,
    wp::float32 var_qd1,
    wp::quat_t<wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1,
    wp::vec_t<3, wp::float32> & adj_axis_0,
    wp::vec_t<3, wp::float32> & adj_axis_1,
    wp::float32 & adj_q0,
    wp::float32 & adj_q1,
    wp::float32 & adj_qd0,
    wp::float32 & adj_qd1,
    wp::quat_t<wp::float32> & adj_ret_0,
    wp::vec_t<3, wp::float32> & adj_ret_1)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/sim/articulation.py:106
static void adj_transform_3d_rotational_axes_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::vec_t<3, wp::float32> var_axis_2,
    wp::float32 var_q0,
    wp::float32 var_q1,
    wp::vec_t<3, wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1,
    wp::vec_t<3, wp::float32> & ret_2,
    wp::vec_t<3, wp::float32> & adj_axis_0,
    wp::vec_t<3, wp::float32> & adj_axis_1,
    wp::vec_t<3, wp::float32> & adj_axis_2,
    wp::float32 & adj_q0,
    wp::float32 & adj_q1,
    wp::vec_t<3, wp::float32> & adj_ret_0,
    wp::vec_t<3, wp::float32> & adj_ret_1,
    wp::vec_t<3, wp::float32> & adj_ret_2)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/sim/articulation.py:130
static void adj_compute_3d_rotational_dofs_0(
    wp::vec_t<3, wp::float32> var_axis_0,
    wp::vec_t<3, wp::float32> var_axis_1,
    wp::vec_t<3, wp::float32> var_axis_2,
    wp::float32 var_q0,
    wp::float32 var_q1,
    wp::float32 var_q2,
    wp::float32 var_qd0,
    wp::float32 var_qd1,
    wp::float32 var_qd2,
    wp::quat_t<wp::float32> & ret_0,
    wp::vec_t<3, wp::float32> & ret_1,
    wp::vec_t<3, wp::float32> & adj_axis_0,
    wp::vec_t<3, wp::float32> & adj_axis_1,
    wp::vec_t<3, wp::float32> & adj_axis_2,
    wp::float32 & adj_q0,
    wp::float32 & adj_q1,
    wp::float32 & adj_q2,
    wp::float32 & adj_qd0,
    wp::float32 & adj_qd1,
    wp::float32 & adj_qd2,
    wp::quat_t<wp::float32> & adj_ret_0,
    wp::vec_t<3, wp::float32> & adj_ret_1)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:262
static void adj_jcalc_transform_0(
    wp::int32 var_type,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::int32 var_axis_start,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::array_t<wp::float32> var_joint_q,
    wp::int32 var_q_start,
    wp::int32 & adj_type,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_joint_axis,
    wp::int32 & adj_axis_start,
    wp::int32 & adj_lin_axis_count,
    wp::int32 & adj_ang_axis_count,
    wp::array_t<wp::float32> & adj_joint_q,
    wp::int32 & adj_q_start,
    wp::transform_t<wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:753
static void adj_compute_link_transform_0(
    wp::int32 var_i,
    wp::array_t<wp::int32> var_joint_type,
    wp::array_t<wp::int32> var_joint_parent,
    wp::array_t<wp::int32> var_joint_child,
    wp::array_t<wp::int32> var_joint_q_start,
    wp::array_t<wp::int32> var_joint_qd_start,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c,
    wp::array_t<wp::transform_t<wp::float32>> var_body_X_com,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::int32> var_joint_dof_dim,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com,
    wp::int32 & adj_i,
    wp::array_t<wp::int32> & adj_joint_type,
    wp::array_t<wp::int32> & adj_joint_parent,
    wp::array_t<wp::int32> & adj_joint_child,
    wp::array_t<wp::int32> & adj_joint_q_start,
    wp::array_t<wp::int32> & adj_joint_qd_start,
    wp::array_t<wp::float32> & adj_joint_q,
    wp::array_t<wp::transform_t<wp::float32>> & adj_joint_X_p,
    wp::array_t<wp::transform_t<wp::float32>> & adj_joint_X_c,
    wp::array_t<wp::transform_t<wp::float32>> & adj_body_X_com,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_joint_axis,
    wp::array_t<wp::int32> & adj_joint_dof_dim,
    wp::array_t<wp::transform_t<wp::float32>> & adj_body_q,
    wp::array_t<wp::transform_t<wp::float32>> & adj_body_q_com)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// warp/_src/math.py:131
static void adj_velocity_at_point_0(
    wp::vec_t<6, wp::float32> var_qd,
    wp::vec_t<3, wp::float32> var_r,
    wp::vec_t<6, wp::float32> & adj_qd,
    wp::vec_t<3, wp::float32> & adj_r,
    wp::vec_t<3, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/math/spatial.py:53
static void adj_velocity_at_point_1(
    wp::vec_t<6, wp::float32> var_qd,
    wp::vec_t<3, wp::float32> var_r,
    wp::vec_t<6, wp::float32> & adj_qd,
    wp::vec_t<3, wp::float32> & adj_r,
    wp::vec_t<3, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/sim/articulation.py:17
static void adj_com_twist_to_point_velocity_0(
    wp::vec_t<6, wp::float32> var_qd,
    wp::transform_t<wp::float32> var_X_wb,
    wp::vec_t<3, wp::float32> var_body_com,
    wp::vec_t<3, wp::float32> var_point,
    wp::vec_t<6, wp::float32> & adj_qd,
    wp::transform_t<wp::float32> & adj_X_wb,
    wp::vec_t<3, wp::float32> & adj_body_com,
    wp::vec_t<3, wp::float32> & adj_point,
    wp::vec_t<3, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// warp/_src/math.py:452
static void adj_transform_twist_0(
    wp::transform_t<wp::float32> var_t,
    wp::vec_t<6, wp::float32> var_x,
    wp::transform_t<wp::float32> & adj_t,
    wp::vec_t<6, wp::float32> & adj_x,
    wp::vec_t<6, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/math/spatial.py:81
static void adj_transform_twist_1(
    wp::transform_t<wp::float32> var_t,
    wp::vec_t<6, wp::float32> var_x,
    wp::transform_t<wp::float32> & adj_t,
    wp::vec_t<6, wp::float32> & adj_x,
    wp::vec_t<6, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/sim/articulation.py:23
static void adj_origin_twist_to_com_twist_0(
    wp::vec_t<6, wp::float32> var_qd,
    wp::transform_t<wp::float32> var_X_wb,
    wp::vec_t<3, wp::float32> var_body_com,
    wp::vec_t<6, wp::float32> & adj_qd,
    wp::transform_t<wp::float32> & adj_X_wb,
    wp::vec_t<3, wp::float32> & adj_body_com,
    wp::vec_t<6, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:1996
static void adj_eval_single_articulation_fk_with_velocity_conversion_0(
    wp::int32 var_joint_start,
    wp::int32 var_joint_end,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::int32> var_joint_q_start,
    wp::array_t<wp::int32> var_joint_qd_start,
    wp::array_t<wp::int32> var_joint_type,
    wp::array_t<wp::int32> var_joint_parent,
    wp::array_t<wp::int32> var_joint_child,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::int32> var_joint_dof_dim,
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd,
    wp::int32 & adj_joint_start,
    wp::int32 & adj_joint_end,
    wp::array_t<wp::float32> & adj_joint_q,
    wp::array_t<wp::float32> & adj_joint_qd,
    wp::array_t<wp::int32> & adj_joint_q_start,
    wp::array_t<wp::int32> & adj_joint_qd_start,
    wp::array_t<wp::int32> & adj_joint_type,
    wp::array_t<wp::int32> & adj_joint_parent,
    wp::array_t<wp::int32> & adj_joint_child,
    wp::array_t<wp::transform_t<wp::float32>> & adj_joint_X_p,
    wp::array_t<wp::transform_t<wp::float32>> & adj_joint_X_c,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_joint_axis,
    wp::array_t<wp::int32> & adj_joint_dof_dim,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_body_com,
    wp::array_t<wp::transform_t<wp::float32>> & adj_body_q,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_body_qd)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:1545
static void adj_spatial_mass_0(
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_s,
    wp::int32 var_joint_start,
    wp::int32 var_joint_count,
    wp::int32 var_M_start,
    wp::array_t<wp::float32> var_M,
    wp::array_t<wp::mat_t<6, 6, wp::float32>> & adj_body_I_s,
    wp::int32 & adj_joint_start,
    wp::int32 & adj_joint_count,
    wp::int32 & adj_M_start,
    wp::array_t<wp::float32> & adj_M)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:1583
static void adj_dense_gemm_0(
    wp::int32 var_m,
    wp::int32 var_n,
    wp::int32 var_p,
    bool var_transpose_A,
    bool var_transpose_B,
    wp::int32 var_A_start,
    wp::int32 var_B_start,
    wp::int32 var_C_start,
    wp::array_t<wp::float32> var_A,
    wp::array_t<wp::float32> var_B,
    wp::array_t<wp::float32> var_C,
    wp::int32 & adj_m,
    wp::int32 & adj_n,
    wp::int32 & adj_p,
    bool & adj_transpose_A,
    bool & adj_transpose_B,
    wp::int32 & adj_A_start,
    wp::int32 & adj_B_start,
    wp::int32 & adj_C_start,
    wp::array_t<wp::float32> & adj_A,
    wp::array_t<wp::float32> & adj_B,
    wp::array_t<wp::float32> & adj_C)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:1905
static void adj_integrate_body_pose_from_com_twist_0(
    wp::transform_t<wp::float32> var_X_wb,
    wp::vec_t<3, wp::float32> var_body_com,
    wp::vec_t<6, wp::float32> var_qd_com_world,
    wp::float32 var_dt,
    wp::transform_t<wp::float32> & adj_X_wb,
    wp::vec_t<3, wp::float32> & adj_body_com,
    wp::vec_t<6, wp::float32> & adj_qd_com_world,
    wp::float32 & adj_dt,
    wp::transform_t<wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/semi_implicit/kernels_body.py:18
static void adj_joint_force_0(
    wp::float32 var_q,
    wp::float32 var_qd,
    wp::float32 var_joint_target_q,
    wp::float32 var_joint_target_qd,
    wp::float32 var_target_ke,
    wp::float32 var_target_kd,
    wp::float32 var_limit_lower,
    wp::float32 var_limit_upper,
    wp::float32 var_limit_ke,
    wp::float32 var_limit_kd,
    wp::float32 var_damping,
    wp::float32 & adj_q,
    wp::float32 & adj_qd,
    wp::float32 & adj_joint_target_q,
    wp::float32 & adj_joint_target_qd,
    wp::float32 & adj_target_ke,
    wp::float32 & adj_target_kd,
    wp::float32 & adj_limit_lower,
    wp::float32 & adj_limit_upper,
    wp::float32 & adj_limit_ke,
    wp::float32 & adj_limit_kd,
    wp::float32 & adj_damping,
    wp::float32 & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:503
static void adj_jcalc_tau_0(
    wp::int32 var_type,
    wp::array_t<wp::float32> var_joint_target_ke,
    wp::array_t<wp::float32> var_joint_target_kd,
    wp::array_t<wp::float32> var_joint_limit_ke,
    wp::array_t<wp::float32> var_joint_limit_kd,
    wp::array_t<wp::float32> var_joint_damping,
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::float32> var_joint_f,
    wp::array_t<wp::float32> var_joint_target_q,
    wp::array_t<wp::float32> var_joint_target_qd,
    wp::array_t<wp::float32> var_joint_limit_lower,
    wp::array_t<wp::float32> var_joint_limit_upper,
    wp::int32 var_coord_start,
    wp::int32 var_dof_start,
    wp::int32 var_target_q_start,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::vec_t<6, wp::float32> var_body_f_s,
    wp::array_t<wp::float32> var_tau,
    wp::int32 & adj_type,
    wp::array_t<wp::float32> & adj_joint_target_ke,
    wp::array_t<wp::float32> & adj_joint_target_kd,
    wp::array_t<wp::float32> & adj_joint_limit_ke,
    wp::array_t<wp::float32> & adj_joint_limit_kd,
    wp::array_t<wp::float32> & adj_joint_damping,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_joint_S_s,
    wp::array_t<wp::float32> & adj_joint_q,
    wp::array_t<wp::float32> & adj_joint_qd,
    wp::array_t<wp::float32> & adj_joint_f,
    wp::array_t<wp::float32> & adj_joint_target_q,
    wp::array_t<wp::float32> & adj_joint_target_qd,
    wp::array_t<wp::float32> & adj_joint_limit_lower,
    wp::array_t<wp::float32> & adj_joint_limit_upper,
    wp::int32 & adj_coord_start,
    wp::int32 & adj_dof_start,
    wp::int32 & adj_target_q_start,
    wp::int32 & adj_lin_axis_count,
    wp::int32 & adj_ang_axis_count,
    wp::vec_t<6, wp::float32> & adj_body_f_s,
    wp::array_t<wp::float32> & adj_tau)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:362
static void adj_jcalc_motion_0(
    wp::int32 var_type,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::float32> var_joint_q,
    wp::int32 var_lin_axis_count,
    wp::int32 var_ang_axis_count,
    wp::transform_t<wp::float32> var_X_sc,
    wp::array_t<wp::float32> var_joint_qd,
    wp::int32 var_q_start,
    wp::int32 var_qd_start,
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s,
    wp::vec_t<6, wp::float32> & ret_0,
    wp::vec_t<6, wp::float32> & ret_1,
    wp::int32 & adj_type,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_joint_axis,
    wp::array_t<wp::float32> & adj_joint_q,
    wp::int32 & adj_lin_axis_count,
    wp::int32 & adj_ang_axis_count,
    wp::transform_t<wp::float32> & adj_X_sc,
    wp::array_t<wp::float32> & adj_joint_qd,
    wp::int32 & adj_q_start,
    wp::int32 & adj_qd_start,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_joint_S_s,
    wp::vec_t<6, wp::float32> & adj_ret_0,
    wp::vec_t<6, wp::float32> & adj_ret_1)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:851
static void adj_spatial_cross_0(
    wp::vec_t<6, wp::float32> var_a,
    wp::vec_t<6, wp::float32> var_b,
    wp::vec_t<6, wp::float32> & adj_a,
    wp::vec_t<6, wp::float32> & adj_b,
    wp::vec_t<6, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:186
static void adj_transform_spatial_inertia_1(
    wp::transform_t<wp::float32> var_t,
    wp::mat_t<6, 6, wp::float32> var_I,
    wp::transform_t<wp::float32> & adj_t,
    wp::mat_t<6, 6, wp::float32> & adj_I,
    wp::mat_t<6, 6, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:865
static void adj_spatial_cross_dual_0(
    wp::vec_t<6, wp::float32> var_a,
    wp::vec_t<6, wp::float32> var_b,
    wp::vec_t<6, wp::float32> & adj_a,
    wp::vec_t<6, wp::float32> & adj_b,
    wp::vec_t<6, wp::float32> & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/featherstone/kernels.py:884
static void adj_compute_link_velocity_0(
    wp::int32 var_i,
    wp::vec_t<3, wp::float32> var_solve_origin,
    wp::array_t<wp::int32> var_joint_type,
    wp::array_t<wp::int32> var_joint_parent,
    wp::array_t<wp::int32> var_joint_child,
    wp::array_t<wp::int32> var_joint_q_start,
    wp::array_t<wp::int32> var_joint_qd_start,
    wp::array_t<wp::float32> var_joint_q,
    wp::array_t<wp::float32> var_joint_qd,
    wp::array_t<wp::float32> var_joint_qdd,
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis,
    wp::array_t<wp::int32> var_joint_dof_dim,
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_m,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com,
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p,
    wp::array_t<wp::int32> var_body_world,
    wp::array_t<wp::vec_t<3, wp::float32>> var_gravity,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd,
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s,
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_solve_origin,
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_s,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_v_s,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_s,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_a_s,
    wp::int32 & adj_i,
    wp::vec_t<3, wp::float32> & adj_solve_origin,
    wp::array_t<wp::int32> & adj_joint_type,
    wp::array_t<wp::int32> & adj_joint_parent,
    wp::array_t<wp::int32> & adj_joint_child,
    wp::array_t<wp::int32> & adj_joint_q_start,
    wp::array_t<wp::int32> & adj_joint_qd_start,
    wp::array_t<wp::float32> & adj_joint_q,
    wp::array_t<wp::float32> & adj_joint_qd,
    wp::array_t<wp::float32> & adj_joint_qdd,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_joint_axis,
    wp::array_t<wp::int32> & adj_joint_dof_dim,
    wp::array_t<wp::mat_t<6, 6, wp::float32>> & adj_body_I_m,
    wp::array_t<wp::transform_t<wp::float32>> & adj_body_q,
    wp::array_t<wp::transform_t<wp::float32>> & adj_body_q_com,
    wp::array_t<wp::transform_t<wp::float32>> & adj_joint_X_p,
    wp::array_t<wp::int32> & adj_body_world,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_gravity,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_body_qd,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_joint_S_s,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_body_solve_origin,
    wp::array_t<wp::mat_t<6, 6, wp::float32>> & adj_body_I_s,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_body_v_s,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_body_f_s,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_body_a_s)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}

struct wp_args_integrate_generalized_joints {
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_dof_dim;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_c;
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::float32> joint_q;
    wp::array_t<wp::float32> joint_qd;
    wp::array_t<wp::float32> joint_qdd;
    wp::float32 dt;
    wp::array_t<wp::float32> joint_q_new;
    wp::array_t<wp::float32> joint_qd_new;
};


template <bool wp_linear_contiguous>
static void integrate_generalized_joints_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_integrate_generalized_joints *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;

    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_dof_dim = _wp_args->joint_dof_dim;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c = _wp_args->joint_X_c;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::float32> var_joint_q = _wp_args->joint_q;
    wp::array_t<wp::float32> var_joint_qd = _wp_args->joint_qd;
    wp::array_t<wp::float32> var_joint_qdd = _wp_args->joint_qdd;
    wp::float32 var_dt = _wp_args->dt;
    wp::array_t<wp::float32> var_joint_q_new = _wp_args->joint_q_new;
    wp::array_t<wp::float32> var_joint_qd_new = _wp_args->joint_qd_new;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32* var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::int32* var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    wp::int32* var_13{};
    wp::int32 var_14{};
    wp::int32 var_15{};
    const wp::int32 var_16 = 0;
    wp::int32* var_17{};
    wp::int32 var_18{};
    wp::int32 var_19{};
    const wp::int32 var_20 = 1;
    wp::int32* var_21{};
    wp::int32 var_22{};
    wp::int32 var_23{};
    wp::transform_t<wp::float32>* var_24{};
    wp::vec_t<3, wp::float32>* var_25{};
    wp::transform_t<wp::float32> var_26{};
    wp::vec_t<3, wp::float32> var_27{};
    //---------
    // forward
    // def integrate_generalized_joints(                                                      <L 1859>
    // index = wp.tid()                                                                       <L 1877>
    var_0 = builtin_tid1d();
    // type = joint_type[index]                                                               <L 1879>
    var_1 = wp::address(var_joint_type, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // parent = joint_parent[index]                                                           <L 1880>
    var_4 = wp::address(var_joint_parent, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // child = joint_child[index]                                                             <L 1881>
    var_7 = wp::address(var_joint_child, var_0);
    var_9 = wp::load(var_7);
    var_8 = wp::copy(var_9);
    // coord_start = joint_q_start[index]                                                     <L 1882>
    var_10 = wp::address(var_joint_q_start, var_0);
    var_12 = wp::load(var_10);
    var_11 = wp::copy(var_12);
    // dof_start = joint_qd_start[index]                                                      <L 1883>
    var_13 = wp::address(var_joint_qd_start, var_0);
    var_15 = wp::load(var_13);
    var_14 = wp::copy(var_15);
    // lin_axis_count = joint_dof_dim[index, 0]                                               <L 1884>
    var_17 = wp::address(var_joint_dof_dim, var_0, var_16);
    var_19 = wp::load(var_17);
    var_18 = wp::copy(var_19);
    // ang_axis_count = joint_dof_dim[index, 1]                                               <L 1885>
    var_21 = wp::address(var_joint_dof_dim, var_0, var_20);
    var_23 = wp::load(var_21);
    var_22 = wp::copy(var_23);
    // jcalc_integrate(                                                                       <L 1887>
    // parent,                                                                                <L 1888>
    // joint_X_c[index],                                                                      <L 1889>
    var_24 = wp::address(var_joint_X_c, var_0);
    // body_com[child],                                                                       <L 1890>
    var_25 = wp::address(var_body_com, var_8);
    // type,                                                                                  <L 1891>
    // joint_q,                                                                               <L 1892>
    // joint_qd,                                                                              <L 1893>
    // joint_qdd,                                                                             <L 1894>
    // coord_start,                                                                           <L 1895>
    // dof_start,                                                                             <L 1896>
    // lin_axis_count,                                                                        <L 1897>
    // ang_axis_count,                                                                        <L 1898>
    // dt,                                                                                    <L 1899>
    // joint_q_new,                                                                           <L 1900>
    // joint_qd_new,                                                                          <L 1901>
    var_26 = wp::load(var_24);
    var_27 = wp::load(var_25);
    jcalc_integrate_0(var_5, var_26, var_27, var_2, var_joint_q, var_joint_qd, var_joint_qdd, var_11, var_14, var_18, var_22, var_dt, var_joint_q_new, var_joint_qd_new);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void integrate_generalized_joints_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_integrate_generalized_joints *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        integrate_generalized_joints_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void integrate_generalized_joints_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_integrate_generalized_joints *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    integrate_generalized_joints_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void integrate_generalized_joints_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_integrate_generalized_joints *_wp_args)
{
    integrate_generalized_joints_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_compute_com_transforms {
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::transform_t<wp::float32>> body_X_com;
};


template <bool wp_linear_contiguous>
static void compute_com_transforms_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_compute_com_transforms *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_compute_com_transforms wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.body_com.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.body_X_com.strides[0] = sizeof(wp::transform_t<wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::transform_t<wp::float32>> var_body_X_com = _wp_args->body_X_com;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::vec_t<3, wp::float32>* var_1{};
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<3, wp::float32> var_3{};
    wp::quat_t<wp::float32> var_4{};
    wp::transform_t<wp::float32> var_5{};
    //---------
    // forward
    // def compute_com_transforms(                                                            <L 46>
    // tid = wp.tid()                                                                         <L 51>
    var_0 = builtin_tid1d();
    // com = body_com[tid]                                                                    <L 52>
    var_1 = wp::address(var_body_com, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // body_X_com[tid] = wp.transform(com, wp.quat_identity())                                <L 53>
    var_4 = wp::quat_identity<wp::float32>();
    var_5 = wp::transform_t<wp::float32>(var_2, var_4);
    wp::array_store(var_body_X_com, var_0, var_5);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void compute_com_transforms_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_com_transforms *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        compute_com_transforms_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void compute_com_transforms_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_com_transforms *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->body_com.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->body_X_com.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)));
    if (wp_linear_contiguous)
        compute_com_transforms_cpu_forward_tasks<true>(dim, _wp_args);
    else
        compute_com_transforms_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void compute_com_transforms_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_com_transforms *_wp_args)
{
    compute_com_transforms_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_convert_free_distance_joint_qd_internal_to_public {
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::float32> joint_qd_internal;
    wp::array_t<wp::float32> joint_qd_public;
};


template <bool wp_linear_contiguous>
static void convert_free_distance_joint_qd_internal_to_public_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_convert_free_distance_joint_qd_internal_to_public *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_convert_free_distance_joint_qd_internal_to_public wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_type.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_parent.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_X_p.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_q.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_com.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.joint_qd_internal.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.joint_qd_public.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::float32> var_joint_qd_internal = _wp_args->joint_qd_internal;
    wp::array_t<wp::float32> var_joint_qd_public = _wp_args->joint_qd_public;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    const wp::int32 var_4 = 1;
    wp::int32 var_5{};
    wp::int32* var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32* var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    bool var_12{};
    const wp::int32 var_13 = 4;
    bool var_14{};
    const wp::int32 var_15 = 5;
    bool var_16{};
    wp::range_t var_17{};
    wp::int32 var_18{};
    wp::float32* var_19{};
    wp::float32 var_20{};
    wp::float32 var_21{};
    wp::int32* var_22{};
    wp::int32 var_23{};
    wp::int32 var_24{};
    wp::int32* var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    wp::transform_t<wp::float32>* var_28{};
    wp::transform_t<wp::float32> var_29{};
    wp::transform_t<wp::float32> var_30{};
    const wp::int32 var_31 = 0;
    bool var_32{};
    wp::transform_t<wp::float32>* var_33{};
    wp::transform_t<wp::float32> var_34{};
    wp::transform_t<wp::float32> var_35{};
    wp::transform_t<wp::float32> var_36{};
    wp::quat_t<wp::float32> var_37{};
    wp::vec_t<3, wp::float32> var_38{};
    wp::transform_t<wp::float32>* var_39{};
    wp::vec_t<3, wp::float32>* var_40{};
    wp::vec_t<3, wp::float32> var_41{};
    wp::transform_t<wp::float32> var_42{};
    wp::vec_t<3, wp::float32> var_43{};
    wp::vec_t<3, wp::float32> var_44{};
    wp::vec_t<3, wp::float32> var_45{};
    const wp::int32 var_46 = 0;
    wp::int32 var_47{};
    wp::float32* var_48{};
    const wp::int32 var_49 = 1;
    wp::int32 var_50{};
    wp::float32* var_51{};
    const wp::int32 var_52 = 2;
    wp::int32 var_53{};
    wp::float32* var_54{};
    wp::vec_t<3, wp::float32> var_55{};
    wp::float32 var_56{};
    wp::float32 var_57{};
    wp::float32 var_58{};
    const wp::int32 var_59 = 3;
    wp::int32 var_60{};
    wp::float32* var_61{};
    const wp::int32 var_62 = 4;
    wp::int32 var_63{};
    wp::float32* var_64{};
    const wp::int32 var_65 = 5;
    wp::int32 var_66{};
    wp::float32* var_67{};
    wp::vec_t<3, wp::float32> var_68{};
    wp::float32 var_69{};
    wp::float32 var_70{};
    wp::float32 var_71{};
    wp::vec_t<3, wp::float32> var_72{};
    wp::vec_t<3, wp::float32> var_73{};
    const wp::int32 var_74 = 0;
    wp::float32 var_75{};
    const wp::int32 var_76 = 0;
    wp::int32 var_77{};
    const wp::int32 var_78 = 1;
    wp::float32 var_79{};
    const wp::int32 var_80 = 1;
    wp::int32 var_81{};
    const wp::int32 var_82 = 2;
    wp::float32 var_83{};
    const wp::int32 var_84 = 2;
    wp::int32 var_85{};
    const wp::int32 var_86 = 0;
    wp::float32 var_87{};
    const wp::int32 var_88 = 3;
    wp::int32 var_89{};
    const wp::int32 var_90 = 1;
    wp::float32 var_91{};
    const wp::int32 var_92 = 4;
    wp::int32 var_93{};
    const wp::int32 var_94 = 2;
    wp::float32 var_95{};
    const wp::int32 var_96 = 5;
    wp::int32 var_97{};
    //---------
    // forward
    // def convert_free_distance_joint_qd_internal_to_public(                                 <L 1140>
    // joint_id = wp.tid()                                                                    <L 1151>
    var_0 = builtin_tid1d();
    // qd_start = joint_qd_start[joint_id]                                                    <L 1152>
    var_1 = wp::address(var_joint_qd_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // qd_end = joint_qd_start[joint_id + 1]                                                  <L 1153>
    var_5 = wp::add(var_0, var_4);
    var_6 = wp::address(var_joint_qd_start, var_5);
    var_8 = wp::load(var_6);
    var_7 = wp::copy(var_8);
    // jtype = joint_type[joint_id]                                                           <L 1154>
    var_9 = wp::address(var_joint_type, var_0);
    var_11 = wp::load(var_9);
    var_10 = wp::copy(var_11);
    // if jtype != JointType.FREE and jtype != JointType.DISTANCE:                            <L 1156>
    var_14 = (var_10 != var_13);
    var_12 = var_14;
    if (var_12) {
        var_16 = (var_10 != var_15);
        var_12 = var_12 && var_16;
    }
    if (var_12) {
        // for i in range(qd_start, qd_end):                                                  <L 1157>
        var_17 = wp::range(var_2, var_7);
        start_for_0:;
            if (iter_cmp(var_17) == 0) goto end_for_0;
            var_18 = wp::iter_next(var_17);
            // joint_qd_public[i] = joint_qd_internal[i]                                      <L 1158>
            var_19 = wp::address(var_joint_qd_internal, var_18);
            var_21 = wp::load(var_19);
            var_20 = wp::copy(var_21);
            wp::array_store(var_joint_qd_public, var_18, var_20);
            goto start_for_0;
        end_for_0:;
        // return                                                                             <L 1159>
        return;
    }
    // parent = joint_parent[joint_id]                                                        <L 1161>
    var_22 = wp::address(var_joint_parent, var_0);
    var_24 = wp::load(var_22);
    var_23 = wp::copy(var_24);
    // child = joint_child[joint_id]                                                          <L 1162>
    var_25 = wp::address(var_joint_child, var_0);
    var_27 = wp::load(var_25);
    var_26 = wp::copy(var_27);
    // X_wpj = joint_X_p[joint_id]                                                            <L 1164>
    var_28 = wp::address(var_joint_X_p, var_0);
    var_30 = wp::load(var_28);
    var_29 = wp::copy(var_30);
    // if parent >= 0:                                                                        <L 1165>
    var_32 = (var_23 >= var_31);
    if (var_32) {
        // X_wpj = body_q[parent] * X_wpj                                                     <L 1166>
        var_33 = wp::address(var_body_q, var_23);
        var_35 = wp::load(var_33);
        var_34 = wp::mul(var_35, var_29);
    }
    var_36 = wp::where(var_32, var_34, var_29);
    // q_p = wp.transform_get_rotation(X_wpj)                                                 <L 1168>
    var_37 = wp::transform_get_rotation(var_36);
    // x_anchor_world = wp.transform_get_translation(X_wpj)                                   <L 1169>
    var_38 = wp::transform_get_translation(var_36);
    // x_child_com_world = wp.transform_point(body_q[child], body_com[child])                 <L 1170>
    var_39 = wp::address(var_body_q, var_26);
    var_40 = wp::address(var_body_com, var_26);
    var_42 = wp::load(var_39);
    var_43 = wp::load(var_40);
    var_41 = wp::transform_point(var_42, var_43);
    // r_child_com_parent = wp.quat_rotate_inv(q_p, x_child_com_world - x_anchor_world)       <L 1171>
    var_44 = wp::sub(var_41, var_38);
    var_45 = wp::quat_rotate_inv(var_37, var_44);
    // v_internal_parent = wp.vec3(                                                           <L 1173>
    // joint_qd_internal[qd_start + 0],                                                       <L 1174>
    var_47 = wp::add(var_2, var_46);
    var_48 = wp::address(var_joint_qd_internal, var_47);
    // joint_qd_internal[qd_start + 1],                                                       <L 1175>
    var_50 = wp::add(var_2, var_49);
    var_51 = wp::address(var_joint_qd_internal, var_50);
    // joint_qd_internal[qd_start + 2],                                                       <L 1176>
    var_53 = wp::add(var_2, var_52);
    var_54 = wp::address(var_joint_qd_internal, var_53);
    var_56 = wp::load(var_48);
    var_57 = wp::load(var_51);
    var_58 = wp::load(var_54);
    var_55 = wp::vec_t<3, wp::float32>(var_56, var_57, var_58);
    // omega_parent = wp.vec3(                                                                <L 1178>
    // joint_qd_internal[qd_start + 3],                                                       <L 1179>
    var_60 = wp::add(var_2, var_59);
    var_61 = wp::address(var_joint_qd_internal, var_60);
    // joint_qd_internal[qd_start + 4],                                                       <L 1180>
    var_63 = wp::add(var_2, var_62);
    var_64 = wp::address(var_joint_qd_internal, var_63);
    // joint_qd_internal[qd_start + 5],                                                       <L 1181>
    var_66 = wp::add(var_2, var_65);
    var_67 = wp::address(var_joint_qd_internal, var_66);
    var_69 = wp::load(var_61);
    var_70 = wp::load(var_64);
    var_71 = wp::load(var_67);
    var_68 = wp::vec_t<3, wp::float32>(var_69, var_70, var_71);
    // v_com_parent = v_internal_parent + wp.cross(omega_parent, r_child_com_parent)          <L 1183>
    var_72 = wp::cross(var_68, var_45);
    var_73 = wp::add(var_55, var_72);
    // joint_qd_public[qd_start + 0] = v_com_parent[0]                                        <L 1185>
    var_75 = wp::extract(var_73, var_74);
    var_77 = wp::add(var_2, var_76);
    wp::array_store(var_joint_qd_public, var_77, var_75);
    // joint_qd_public[qd_start + 1] = v_com_parent[1]                                        <L 1186>
    var_79 = wp::extract(var_73, var_78);
    var_81 = wp::add(var_2, var_80);
    wp::array_store(var_joint_qd_public, var_81, var_79);
    // joint_qd_public[qd_start + 2] = v_com_parent[2]                                        <L 1187>
    var_83 = wp::extract(var_73, var_82);
    var_85 = wp::add(var_2, var_84);
    wp::array_store(var_joint_qd_public, var_85, var_83);
    // joint_qd_public[qd_start + 3] = omega_parent[0]                                        <L 1188>
    var_87 = wp::extract(var_68, var_86);
    var_89 = wp::add(var_2, var_88);
    wp::array_store(var_joint_qd_public, var_89, var_87);
    // joint_qd_public[qd_start + 4] = omega_parent[1]                                        <L 1189>
    var_91 = wp::extract(var_68, var_90);
    var_93 = wp::add(var_2, var_92);
    wp::array_store(var_joint_qd_public, var_93, var_91);
    // joint_qd_public[qd_start + 5] = omega_parent[2]                                        <L 1190>
    var_95 = wp::extract(var_68, var_94);
    var_97 = wp::add(var_2, var_96);
    wp::array_store(var_joint_qd_public, var_97, var_95);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void convert_free_distance_joint_qd_internal_to_public_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_qd_internal_to_public *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        convert_free_distance_joint_qd_internal_to_public_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void convert_free_distance_joint_qd_internal_to_public_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_qd_internal_to_public *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_type.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_parent.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_X_p.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_q.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_com.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->joint_qd_internal.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->joint_qd_public.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        convert_free_distance_joint_qd_internal_to_public_cpu_forward_tasks<true>(dim, _wp_args);
    else
        convert_free_distance_joint_qd_internal_to_public_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void convert_free_distance_joint_qd_internal_to_public_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_qd_internal_to_public *_wp_args)
{
    convert_free_distance_joint_qd_internal_to_public_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_zero_kinematic_body_forces {
    wp::array_t<wp::int32> body_flags;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f;
};


template <bool wp_linear_contiguous>
static void zero_kinematic_body_forces_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_zero_kinematic_body_forces *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_zero_kinematic_body_forces wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.body_flags.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.body_f.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_body_flags = _wp_args->body_flags;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f = _wp_args->body_f;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    const wp::int32 var_2 = 2;
    wp::int32 var_3{};
    wp::int32 var_4{};
    const wp::int32 var_5 = 0;
    bool var_6{};
    wp::vec_t<6, wp::float32> var_7{};
    //---------
    // forward
    // def zero_kinematic_body_forces(                                                        <L 57>
    // tid = wp.tid()                                                                         <L 62>
    var_0 = builtin_tid1d();
    // if (body_flags[tid] & BodyFlags.KINEMATIC) == 0:                                       <L 63>
    var_1 = wp::address(var_body_flags, var_0);
    var_4 = wp::load(var_1);
    var_3 = wp::bit_and(var_4, var_2);
    var_6 = (var_3 == var_5);
    if (var_6) {
        // return                                                                             <L 64>
        return;
    }
    // body_f[tid] = wp.spatial_vector()                                                      <L 65>
    var_7 = wp::vec_t<6, wp::float32>();
    wp::array_store(var_body_f, var_0, var_7);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void zero_kinematic_body_forces_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_zero_kinematic_body_forces *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        zero_kinematic_body_forces_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void zero_kinematic_body_forces_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_zero_kinematic_body_forces *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->body_flags.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->body_f.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)));
    if (wp_linear_contiguous)
        zero_kinematic_body_forces_cpu_forward_tasks<true>(dim, _wp_args);
    else
        zero_kinematic_body_forces_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void zero_kinematic_body_forces_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_zero_kinematic_body_forces *_wp_args)
{
    zero_kinematic_body_forces_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_dense_solve_batched {
    wp::array_t<wp::int32> L_start;
    wp::array_t<wp::int32> L_dim;
    wp::array_t<wp::int32> b_start;
    wp::array_t<wp::float32> A;
    wp::array_t<wp::float32> L;
    wp::array_t<wp::float32> b;
    wp::array_t<wp::float32> x;
    wp::array_t<wp::float32> tmp;
};


template <bool wp_linear_contiguous>
static void eval_dense_solve_batched_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_dense_solve_batched *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_eval_dense_solve_batched wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.L_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.L_dim.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.b_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.A.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.L.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.b.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.x.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.tmp.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_L_start = _wp_args->L_start;
    wp::array_t<wp::int32> var_L_dim = _wp_args->L_dim;
    wp::array_t<wp::int32> var_b_start = _wp_args->b_start;
    wp::array_t<wp::float32> var_A = _wp_args->A;
    wp::array_t<wp::float32> var_L = _wp_args->L;
    wp::array_t<wp::float32> var_b = _wp_args->b;
    wp::array_t<wp::float32> var_x = _wp_args->x;
    wp::array_t<wp::float32> var_tmp = _wp_args->tmp;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32* var_2{};
    wp::int32* var_3{};
    wp::int32 var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    //---------
    // forward
    // def eval_dense_solve_batched(                                                          <L 1842>
    // batch = wp.tid()                                                                       <L 1853>
    var_0 = builtin_tid1d();
    // dense_solve(L_dim[batch], L_start[batch], b_start[batch], A, L, b, x, tmp)             <L 1855>
    var_1 = wp::address(var_L_dim, var_0);
    var_2 = wp::address(var_L_start, var_0);
    var_3 = wp::address(var_b_start, var_0);
    var_4 = wp::load(var_1);
    var_5 = wp::load(var_2);
    var_6 = wp::load(var_3);
    dense_solve_0(var_4, var_5, var_6, var_A, var_L, var_b, var_x, var_tmp);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_dense_solve_batched_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_solve_batched *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_dense_solve_batched_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_dense_solve_batched_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_solve_batched *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->L_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->L_dim.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->b_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->A.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->L.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->b.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->x.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->tmp.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        eval_dense_solve_batched_cpu_forward_tasks<true>(dim, _wp_args);
    else
        eval_dense_solve_batched_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_dense_solve_batched_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_solve_batched *_wp_args)
{
    eval_dense_solve_batched_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_rigid_jacobian {
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> articulation_J_start;
    wp::array_t<wp::int32> joint_ancestor;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::vec_t<6, wp::float32>> joint_S_s;
    wp::array_t<wp::float32> J;
};


template <bool wp_linear_contiguous>
static void eval_rigid_jacobian_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_rigid_jacobian *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_eval_rigid_jacobian wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.articulation_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_end.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_J_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_ancestor.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_S_s.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        wp_contiguous_args.J.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_articulation_J_start = _wp_args->articulation_J_start;
    wp::array_t<wp::int32> var_joint_ancestor = _wp_args->joint_ancestor;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s = _wp_args->joint_S_s;
    wp::array_t<wp::float32> var_J = _wp_args->J;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32 var_7{};
    wp::int32* var_8{};
    wp::int32 var_9{};
    wp::int32 var_10{};
    wp::int32* var_11{};
    wp::int32 var_12{};
    wp::int32 var_13{};
    wp::int32* var_14{};
    wp::int32 var_15{};
    wp::int32 var_16{};
    wp::int32 var_17{};
    wp::range_t var_18{};
    wp::int32 var_19{};
    const wp::int32 var_20 = 6;
    wp::int32 var_21{};
    wp::int32 var_22{};
    const wp::int32 var_23 = -1;
    bool var_24{};
    wp::int32* var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    const wp::int32 var_28 = 1;
    wp::int32 var_29{};
    wp::int32* var_30{};
    wp::int32 var_31{};
    wp::int32 var_32{};
    wp::int32 var_33{};
    wp::range_t var_34{};
    wp::int32 var_35{};
    wp::int32 var_36{};
    wp::int32 var_37{};
    wp::int32 var_38{};
    wp::vec_t<6, wp::float32>* var_39{};
    wp::vec_t<6, wp::float32> var_40{};
    wp::vec_t<6, wp::float32> var_41{};
    const wp::int32 var_42 = 0;
    wp::float32 var_43{};
    wp::int32 var_44{};
    wp::int32 var_45{};
    wp::int32 var_46{};
    const wp::int32 var_47 = 1;
    wp::float32 var_48{};
    wp::int32 var_49{};
    wp::int32 var_50{};
    wp::int32 var_51{};
    const wp::int32 var_52 = 2;
    wp::float32 var_53{};
    wp::int32 var_54{};
    wp::int32 var_55{};
    wp::int32 var_56{};
    const wp::int32 var_57 = 3;
    wp::float32 var_58{};
    wp::int32 var_59{};
    wp::int32 var_60{};
    wp::int32 var_61{};
    const wp::int32 var_62 = 4;
    wp::float32 var_63{};
    wp::int32 var_64{};
    wp::int32 var_65{};
    wp::int32 var_66{};
    const wp::int32 var_67 = 5;
    wp::float32 var_68{};
    wp::int32 var_69{};
    wp::int32 var_70{};
    wp::int32 var_71{};
    wp::int32* var_72{};
    wp::int32 var_73{};
    wp::int32 var_74{};
    //---------
    // forward
    // def eval_rigid_jacobian(                                                               <L 1502>
    // index = wp.tid()                                                                       <L 1513>
    var_0 = builtin_tid1d();
    // joint_start = articulation_start[index]                                                <L 1515>
    var_1 = wp::address(var_articulation_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // joint_end = articulation_end[index]                                                    <L 1516>
    var_4 = wp::address(var_articulation_end, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // joint_count = joint_end - joint_start                                                  <L 1517>
    var_7 = wp::sub(var_5, var_2);
    // J_offset = articulation_J_start[index]                                                 <L 1519>
    var_8 = wp::address(var_articulation_J_start, var_0);
    var_10 = wp::load(var_8);
    var_9 = wp::copy(var_10);
    // articulation_dof_start = joint_qd_start[joint_start]                                   <L 1521>
    var_11 = wp::address(var_joint_qd_start, var_2);
    var_13 = wp::load(var_11);
    var_12 = wp::copy(var_13);
    // articulation_dof_end = joint_qd_start[joint_end]                                       <L 1522>
    var_14 = wp::address(var_joint_qd_start, var_5);
    var_16 = wp::load(var_14);
    var_15 = wp::copy(var_16);
    // articulation_dof_count = articulation_dof_end - articulation_dof_start                 <L 1523>
    var_17 = wp::sub(var_15, var_12);
    // for i in range(joint_count):                                                           <L 1525>
    var_18 = wp::range(var_7);
    start_for_0:;
        if (iter_cmp(var_18) == 0) goto end_for_0;
        var_19 = wp::iter_next(var_18);
        // row_start = i * 6                                                                  <L 1526>
        var_21 = wp::mul(var_19, var_20);
        // j = joint_start + i                                                                <L 1528>
        var_22 = wp::add(var_2, var_19);
        // while j != -1:                                                                     <L 1529>
    start_while_2:;
        var_24 = (var_22 != var_23);
    if ((var_24) == false) goto end_while_2;
            // joint_dof_start = joint_qd_start[j]                                            <L 1530>
            var_25 = wp::address(var_joint_qd_start, var_22);
            var_27 = wp::load(var_25);
            var_26 = wp::copy(var_27);
            // joint_dof_end = joint_qd_start[j + 1]                                          <L 1531>
            var_29 = wp::add(var_22, var_28);
            var_30 = wp::address(var_joint_qd_start, var_29);
            var_32 = wp::load(var_30);
            var_31 = wp::copy(var_32);
            // joint_dof_count = joint_dof_end - joint_dof_start                              <L 1532>
            var_33 = wp::sub(var_31, var_26);
            // for dof in range(joint_dof_count):                                             <L 1535>
            var_34 = wp::range(var_33);
            start_for_4:;
                if (iter_cmp(var_34) == 0) goto end_for_4;
                var_35 = wp::iter_next(var_34);
                // col = (joint_dof_start - articulation_dof_start) + dof                     <L 1536>
                var_36 = wp::sub(var_26, var_12);
                var_37 = wp::add(var_36, var_35);
                // S = joint_S_s[joint_dof_start + dof]                                       <L 1537>
                var_38 = wp::add(var_26, var_35);
                var_39 = wp::address(var_joint_S_s, var_38);
                var_41 = wp::load(var_39);
                var_40 = wp::copy(var_41);
                // for k in range(6):                                                         <L 1539>
                // J[J_offset + dense_index(articulation_dof_count, row_start + k, col)] = S[k]       <L 1540>
                var_43 = wp::extract(var_40, var_42);
                var_44 = wp::add(var_21, var_42);
                var_45 = dense_index_0(var_17, var_44, var_37);
                var_46 = wp::add(var_9, var_45);
                wp::array_store(var_J, var_46, var_43);
                var_48 = wp::extract(var_40, var_47);
                var_49 = wp::add(var_21, var_47);
                var_50 = dense_index_0(var_17, var_49, var_37);
                var_51 = wp::add(var_9, var_50);
                wp::array_store(var_J, var_51, var_48);
                var_53 = wp::extract(var_40, var_52);
                var_54 = wp::add(var_21, var_52);
                var_55 = dense_index_0(var_17, var_54, var_37);
                var_56 = wp::add(var_9, var_55);
                wp::array_store(var_J, var_56, var_53);
                var_58 = wp::extract(var_40, var_57);
                var_59 = wp::add(var_21, var_57);
                var_60 = dense_index_0(var_17, var_59, var_37);
                var_61 = wp::add(var_9, var_60);
                wp::array_store(var_J, var_61, var_58);
                var_63 = wp::extract(var_40, var_62);
                var_64 = wp::add(var_21, var_62);
                var_65 = dense_index_0(var_17, var_64, var_37);
                var_66 = wp::add(var_9, var_65);
                wp::array_store(var_J, var_66, var_63);
                var_68 = wp::extract(var_40, var_67);
                var_69 = wp::add(var_21, var_67);
                var_70 = dense_index_0(var_17, var_69, var_37);
                var_71 = wp::add(var_9, var_70);
                wp::array_store(var_J, var_71, var_68);
                goto start_for_4;
            end_for_4:;
            // j = joint_ancestor[j]                                                          <L 1542>
            var_72 = wp::address(var_joint_ancestor, var_22);
            var_74 = wp::load(var_72);
            var_73 = wp::copy(var_74);
            wp::assign(var_22, var_73);
    goto start_while_2;
    end_while_2:;
        goto start_for_0;
    end_for_0:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_rigid_jacobian_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_jacobian *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_rigid_jacobian_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_rigid_jacobian_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_jacobian *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->articulation_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_end.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_J_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_ancestor.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_S_s.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)) & (_wp_args->J.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        eval_rigid_jacobian_cpu_forward_tasks<true>(dim, _wp_args);
    else
        eval_rigid_jacobian_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_rigid_jacobian_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_jacobian *_wp_args)
{
    eval_rigid_jacobian_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_compute_body_parent_f {
    wp::array_t<wp::transform_t<wp::float32>> body_q_com;
    wp::array_t<wp::vec_t<3, wp::float32>> body_solve_origin;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f_s;
    wp::array_t<wp::vec_t<6, wp::float32>> body_ft_s;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f_ext;
    wp::array_t<wp::vec_t<6, wp::float32>> body_parent_f;
};


template <bool wp_linear_contiguous>
static void compute_body_parent_f_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_compute_body_parent_f *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_compute_body_parent_f wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.body_q_com.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_solve_origin.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.body_f_s.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        wp_contiguous_args.body_ft_s.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        wp_contiguous_args.body_f_ext.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        wp_contiguous_args.body_parent_f.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com = _wp_args->body_q_com;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_solve_origin = _wp_args->body_solve_origin;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_s = _wp_args->body_f_s;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_ft_s = _wp_args->body_ft_s;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_ext = _wp_args->body_f_ext;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_parent_f = _wp_args->body_parent_f;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::vec_t<6, wp::float32>* var_1{};
    wp::vec_t<6, wp::float32>* var_2{};
    wp::vec_t<6, wp::float32> var_3{};
    wp::vec_t<6, wp::float32> var_4{};
    wp::vec_t<6, wp::float32> var_5{};
    wp::vec_t<6, wp::float32>* var_6{};
    wp::vec_t<6, wp::float32> var_7{};
    wp::vec_t<6, wp::float32> var_8{};
    wp::vec_t<3, wp::float32> var_9{};
    wp::vec_t<3, wp::float32> var_10{};
    wp::transform_t<wp::float32>* var_11{};
    wp::vec_t<3, wp::float32> var_12{};
    wp::transform_t<wp::float32> var_13{};
    wp::vec_t<3, wp::float32>* var_14{};
    wp::vec_t<3, wp::float32> var_15{};
    wp::vec_t<3, wp::float32> var_16{};
    wp::vec_t<3, wp::float32> var_17{};
    wp::vec_t<3, wp::float32> var_18{};
    wp::vec_t<6, wp::float32> var_19{};
    //---------
    // forward
    // def compute_body_parent_f(                                                             <L 2381>
    // tid = wp.tid()                                                                         <L 2416>
    var_0 = builtin_tid1d();
    // f_s = body_f_s[tid] + body_ft_s[tid] + body_f_ext[tid]                                 <L 2418>
    var_1 = wp::address(var_body_f_s, var_0);
    var_2 = wp::address(var_body_ft_s, var_0);
    var_4 = wp::load(var_1);
    var_5 = wp::load(var_2);
    var_3 = wp::add(var_4, var_5);
    var_6 = wp::address(var_body_f_ext, var_0);
    var_8 = wp::load(var_6);
    var_7 = wp::add(var_3, var_8);
    // f_lin = wp.spatial_top(f_s)                                                            <L 2419>
    var_9 = wp::spatial_top(var_7);
    // f_ang_at_origin = wp.spatial_bottom(f_s)                                               <L 2420>
    var_10 = wp::spatial_bottom(var_7);
    // r_com = wp.transform_get_translation(body_q_com[tid]) - body_solve_origin[tid]         <L 2422>
    var_11 = wp::address(var_body_q_com, var_0);
    var_13 = wp::load(var_11);
    var_12 = wp::transform_get_translation(var_13);
    var_14 = wp::address(var_body_solve_origin, var_0);
    var_16 = wp::load(var_14);
    var_15 = wp::sub(var_12, var_16);
    // f_ang_at_com = f_ang_at_origin - wp.cross(r_com, f_lin)                                <L 2423>
    var_17 = wp::cross(var_15, var_9);
    var_18 = wp::sub(var_10, var_17);
    // body_parent_f[tid] = wp.spatial_vector(f_lin, f_ang_at_com)                            <L 2425>
    var_19 = wp::vec_t<6, wp::float32>(var_9, var_18);
    wp::array_store(var_body_parent_f, var_0, var_19);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void compute_body_parent_f_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_body_parent_f *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        compute_body_parent_f_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void compute_body_parent_f_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_body_parent_f *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->body_q_com.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_solve_origin.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->body_f_s.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)) & (_wp_args->body_ft_s.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)) & (_wp_args->body_f_ext.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)) & (_wp_args->body_parent_f.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)));
    if (wp_linear_contiguous)
        compute_body_parent_f_cpu_forward_tasks<true>(dim, _wp_args);
    else
        compute_body_parent_f_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void compute_body_parent_f_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_body_parent_f *_wp_args)
{
    compute_body_parent_f_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_convert_free_distance_joint_f_public_to_internal {
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::float32> joint_f_public;
    wp::array_t<wp::float32> joint_f_internal;
};


template <bool wp_linear_contiguous>
static void convert_free_distance_joint_f_public_to_internal_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_convert_free_distance_joint_f_public_to_internal *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_convert_free_distance_joint_f_public_to_internal wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_type.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_f_public.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.joint_f_internal.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::float32> var_joint_f_public = _wp_args->joint_f_public;
    wp::array_t<wp::float32> var_joint_f_internal = _wp_args->joint_f_internal;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    const wp::int32 var_4 = 1;
    wp::int32 var_5{};
    wp::int32* var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32* var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    bool var_12{};
    const wp::int32 var_13 = 4;
    bool var_14{};
    const wp::int32 var_15 = 5;
    bool var_16{};
    wp::range_t var_17{};
    wp::int32 var_18{};
    wp::float32* var_19{};
    wp::float32 var_20{};
    wp::float32 var_21{};
    wp::range_t var_22{};
    wp::int32 var_23{};
    const wp::float32 var_24 = 0.0;
    //---------
    // forward
    // def convert_free_distance_joint_f_public_to_internal(                                  <L 1194>
    // joint_id = wp.tid()                                                                    <L 1201>
    var_0 = builtin_tid1d();
    // qd_start = joint_qd_start[joint_id]                                                    <L 1202>
    var_1 = wp::address(var_joint_qd_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // qd_end = joint_qd_start[joint_id + 1]                                                  <L 1203>
    var_5 = wp::add(var_0, var_4);
    var_6 = wp::address(var_joint_qd_start, var_5);
    var_8 = wp::load(var_6);
    var_7 = wp::copy(var_8);
    // jtype = joint_type[joint_id]                                                           <L 1204>
    var_9 = wp::address(var_joint_type, var_0);
    var_11 = wp::load(var_9);
    var_10 = wp::copy(var_11);
    // if jtype != JointType.FREE and jtype != JointType.DISTANCE:                            <L 1206>
    var_14 = (var_10 != var_13);
    var_12 = var_14;
    if (var_12) {
        var_16 = (var_10 != var_15);
        var_12 = var_12 && var_16;
    }
    if (var_12) {
        // for i in range(qd_start, qd_end):                                                  <L 1207>
        var_17 = wp::range(var_2, var_7);
        start_for_0:;
            if (iter_cmp(var_17) == 0) goto end_for_0;
            var_18 = wp::iter_next(var_17);
            // joint_f_internal[i] = joint_f_public[i]                                        <L 1208>
            var_19 = wp::address(var_joint_f_public, var_18);
            var_21 = wp::load(var_19);
            var_20 = wp::copy(var_21);
            wp::array_store(var_joint_f_internal, var_18, var_20);
            goto start_for_0;
        end_for_0:;
        // return                                                                             <L 1209>
        return;
    }
    // for i in range(qd_start, qd_end):                                                      <L 1211>
    var_22 = wp::range(var_2, var_7);
    start_for_3:;
        if (iter_cmp(var_22) == 0) goto end_for_3;
        var_23 = wp::iter_next(var_22);
        // joint_f_internal[i] = 0.0                                                          <L 1212>
        wp::array_store(var_joint_f_internal, var_23, var_24);
        goto start_for_3;
    end_for_3:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void convert_free_distance_joint_f_public_to_internal_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_f_public_to_internal *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        convert_free_distance_joint_f_public_to_internal_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void convert_free_distance_joint_f_public_to_internal_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_f_public_to_internal *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_type.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_f_public.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->joint_f_internal.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        convert_free_distance_joint_f_public_to_internal_cpu_forward_tasks<true>(dim, _wp_args);
    else
        convert_free_distance_joint_f_public_to_internal_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void convert_free_distance_joint_f_public_to_internal_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_f_public_to_internal *_wp_args)
{
    convert_free_distance_joint_f_public_to_internal_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_rigid_fk {
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::float32> joint_q;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_c;
    wp::array_t<wp::transform_t<wp::float32>> body_X_com;
    wp::array_t<wp::vec_t<3, wp::float32>> joint_axis;
    wp::array_t<wp::int32> joint_dof_dim;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::transform_t<wp::float32>> body_q_com;
};


template <bool wp_linear_contiguous>
static void eval_rigid_fk_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_rigid_fk *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;

    //---------
    // argument vars
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::float32> var_joint_q = _wp_args->joint_q;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c = _wp_args->joint_X_c;
    wp::array_t<wp::transform_t<wp::float32>> var_body_X_com = _wp_args->body_X_com;
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis = _wp_args->joint_axis;
    wp::array_t<wp::int32> var_joint_dof_dim = _wp_args->joint_dof_dim;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com = _wp_args->body_q_com;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::range_t var_7{};
    wp::int32 var_8{};
    //---------
    // forward
    // def eval_rigid_fk(                                                                     <L 808>
    // index = wp.tid()                                                                       <L 827>
    var_0 = builtin_tid1d();
    // start = articulation_start[index]                                                      <L 829>
    var_1 = wp::address(var_articulation_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // end = articulation_end[index]                                                          <L 830>
    var_4 = wp::address(var_articulation_end, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // for i in range(start, end):                                                            <L 832>
    var_7 = wp::range(var_2, var_5);
    start_for_0:;
        if (iter_cmp(var_7) == 0) goto end_for_0;
        var_8 = wp::iter_next(var_7);
        // compute_link_transform(                                                            <L 833>
        // i,                                                                                 <L 834>
        // joint_type,                                                                        <L 835>
        // joint_parent,                                                                      <L 836>
        // joint_child,                                                                       <L 837>
        // joint_q_start,                                                                     <L 838>
        // joint_qd_start,                                                                    <L 839>
        // joint_q,                                                                           <L 840>
        // joint_X_p,                                                                         <L 841>
        // joint_X_c,                                                                         <L 842>
        // body_X_com,                                                                        <L 843>
        // joint_axis,                                                                        <L 844>
        // joint_dof_dim,                                                                     <L 845>
        // body_q,                                                                            <L 846>
        // body_q_com,                                                                        <L 847>
        compute_link_transform_0(var_8, var_joint_type, var_joint_parent, var_joint_child, var_joint_q_start, var_joint_qd_start, var_joint_q, var_joint_X_p, var_joint_X_c, var_body_X_com, var_joint_axis, var_joint_dof_dim, var_body_q, var_body_q_com);
        goto start_for_0;
    end_for_0:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_rigid_fk_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_fk *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_rigid_fk_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_rigid_fk_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_fk *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    eval_rigid_fk_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_rigid_fk_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_fk *_wp_args)
{
    eval_rigid_fk_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_compute_spatial_inertia {
    wp::array_t<wp::mat_t<3, 3, wp::float32>> body_inertia;
    wp::array_t<wp::float32> body_mass;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> body_I_m;
};


template <bool wp_linear_contiguous>
static void compute_spatial_inertia_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_compute_spatial_inertia *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_compute_spatial_inertia wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.body_inertia.strides[0] = sizeof(wp::mat_t<3, 3, wp::float32>);
        wp_contiguous_args.body_mass.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.body_I_m.strides[0] = sizeof(wp::mat_t<6, 6, wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::mat_t<3, 3, wp::float32>> var_body_inertia = _wp_args->body_inertia;
    wp::array_t<wp::float32> var_body_mass = _wp_args->body_mass;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_m = _wp_args->body_I_m;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::mat_t<3, 3, wp::float32>* var_1{};
    wp::mat_t<3, 3, wp::float32> var_2{};
    wp::mat_t<3, 3, wp::float32> var_3{};
    wp::float32* var_4{};
    wp::float32 var_5{};
    wp::float32 var_6{};
    const wp::float32 var_7 = 0.0;
    const wp::float32 var_8 = 0.0;
    const wp::float32 var_9 = 0.0;
    const wp::float32 var_10 = 0.0;
    const wp::float32 var_11 = 0.0;
    const wp::float32 var_12 = 0.0;
    const wp::float32 var_13 = 0.0;
    const wp::float32 var_14 = 0.0;
    const wp::float32 var_15 = 0.0;
    const wp::float32 var_16 = 0.0;
    const wp::float32 var_17 = 0.0;
    const wp::float32 var_18 = 0.0;
    const wp::float32 var_19 = 0.0;
    const wp::float32 var_20 = 0.0;
    const wp::float32 var_21 = 0.0;
    const wp::float32 var_22 = 0.0;
    const wp::float32 var_23 = 0.0;
    const wp::float32 var_24 = 0.0;
    const wp::int32 var_25 = 0;
    const wp::int32 var_26 = 0;
    wp::float32 var_27{};
    const wp::int32 var_28 = 0;
    const wp::int32 var_29 = 1;
    wp::float32 var_30{};
    const wp::int32 var_31 = 0;
    const wp::int32 var_32 = 2;
    wp::float32 var_33{};
    const wp::float32 var_34 = 0.0;
    const wp::float32 var_35 = 0.0;
    const wp::float32 var_36 = 0.0;
    const wp::int32 var_37 = 1;
    const wp::int32 var_38 = 0;
    wp::float32 var_39{};
    const wp::int32 var_40 = 1;
    const wp::int32 var_41 = 1;
    wp::float32 var_42{};
    const wp::int32 var_43 = 1;
    const wp::int32 var_44 = 2;
    wp::float32 var_45{};
    const wp::float32 var_46 = 0.0;
    const wp::float32 var_47 = 0.0;
    const wp::float32 var_48 = 0.0;
    const wp::int32 var_49 = 2;
    const wp::int32 var_50 = 0;
    wp::float32 var_51{};
    const wp::int32 var_52 = 2;
    const wp::int32 var_53 = 1;
    wp::float32 var_54{};
    const wp::int32 var_55 = 2;
    const wp::int32 var_56 = 2;
    wp::float32 var_57{};
    wp::mat_t<6, 6, wp::float32> var_58{};
    //---------
    // forward
    // def compute_spatial_inertia(                                                           <L 24>
    // tid = wp.tid()                                                                         <L 30>
    var_0 = builtin_tid1d();
    // I = body_inertia[tid]                                                                  <L 31>
    var_1 = wp::address(var_body_inertia, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // m = body_mass[tid]                                                                     <L 32>
    var_4 = wp::address(var_body_mass, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // body_I_m[tid] = wp.spatial_matrix(                                                     <L 34>
    // m,   0.0, 0.0, 0.0,     0.0,     0.0,                                                  <L 35>
    // 0.0, m,   0.0, 0.0,     0.0,     0.0,                                                  <L 36>
    // 0.0, 0.0, m,   0.0,     0.0,     0.0,                                                  <L 37>
    // 0.0, 0.0, 0.0, I[0, 0], I[0, 1], I[0, 2],                                              <L 38>
    var_27 = wp::extract(var_2, var_25, var_26);
    var_30 = wp::extract(var_2, var_28, var_29);
    var_33 = wp::extract(var_2, var_31, var_32);
    // 0.0, 0.0, 0.0, I[1, 0], I[1, 1], I[1, 2],                                              <L 39>
    var_39 = wp::extract(var_2, var_37, var_38);
    var_42 = wp::extract(var_2, var_40, var_41);
    var_45 = wp::extract(var_2, var_43, var_44);
    // 0.0, 0.0, 0.0, I[2, 0], I[2, 1], I[2, 2],                                              <L 40>
    var_51 = wp::extract(var_2, var_49, var_50);
    var_54 = wp::extract(var_2, var_52, var_53);
    var_57 = wp::extract(var_2, var_55, var_56);
    var_58 = wp::mat_t<6, 6, wp::float32>({var_5, var_7, var_8, var_9, var_10, var_11, var_12, var_5, var_13, var_14, var_15, var_16, var_17, var_18, var_5, var_19, var_20, var_21, var_22, var_23, var_24, var_27, var_30, var_33, var_34, var_35, var_36, var_39, var_42, var_45, var_46, var_47, var_48, var_51, var_54, var_57});
    // body_I_m[tid] = wp.spatial_matrix(                                                     <L 34>
    wp::array_store(var_body_I_m, var_0, var_58);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void compute_spatial_inertia_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_spatial_inertia *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        compute_spatial_inertia_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void compute_spatial_inertia_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_spatial_inertia *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->body_inertia.strides[0] == (int)sizeof(wp::mat_t<3, 3, wp::float32>)) & (_wp_args->body_mass.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->body_I_m.strides[0] == (int)sizeof(wp::mat_t<6, 6, wp::float32>)));
    if (wp_linear_contiguous)
        compute_spatial_inertia_cpu_forward_tasks<true>(dim, _wp_args);
    else
        compute_spatial_inertia_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void compute_spatial_inertia_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_compute_spatial_inertia *_wp_args)
{
    compute_spatial_inertia_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_convert_free_distance_joint_f_internal_to_public {
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_articulation;
    wp::array_t<bool> articulation_mask;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::transform_t<wp::float32>> body_q_com;
    wp::array_t<wp::float32> joint_f;
};


template <bool wp_linear_contiguous>
static void convert_free_distance_joint_f_internal_to_public_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_convert_free_distance_joint_f_internal_to_public *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_convert_free_distance_joint_f_internal_to_public wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_type.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_parent.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_articulation.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_mask.strides[0] = sizeof(bool);
        wp_contiguous_args.joint_X_p.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_q.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_q_com.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.joint_f.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_articulation = _wp_args->joint_articulation;
    wp::array_t<bool> var_articulation_mask = _wp_args->articulation_mask;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com = _wp_args->body_q_com;
    wp::array_t<wp::float32> var_joint_f = _wp_args->joint_f;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    bool* var_3;
    bool var_4{};
    bool var_5{};
    wp::int32* var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32* var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    const wp::int32 var_12 = 1;
    wp::int32 var_13{};
    wp::int32* var_14{};
    wp::int32 var_15{};
    wp::int32 var_16{};
    bool var_17{};
    const wp::int32 var_18 = 4;
    bool var_19{};
    const wp::int32 var_20 = 5;
    bool var_21{};
    wp::int32* var_22{};
    wp::int32 var_23{};
    wp::int32 var_24{};
    wp::int32* var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    wp::transform_t<wp::float32>* var_28{};
    wp::transform_t<wp::float32> var_29{};
    wp::transform_t<wp::float32> var_30{};
    const wp::int32 var_31 = 0;
    bool var_32{};
    wp::transform_t<wp::float32>* var_33{};
    wp::transform_t<wp::float32> var_34{};
    wp::transform_t<wp::float32> var_35{};
    wp::transform_t<wp::float32> var_36{};
    wp::quat_t<wp::float32> var_37{};
    wp::vec_t<3, wp::float32> var_38{};
    wp::transform_t<wp::float32>* var_39{};
    wp::vec_t<3, wp::float32> var_40{};
    wp::transform_t<wp::float32> var_41{};
    wp::vec_t<3, wp::float32> var_42{};
    wp::vec_t<3, wp::float32> var_43{};
    const wp::int32 var_44 = 0;
    wp::int32 var_45{};
    wp::float32* var_46{};
    const wp::int32 var_47 = 1;
    wp::int32 var_48{};
    wp::float32* var_49{};
    const wp::int32 var_50 = 2;
    wp::int32 var_51{};
    wp::float32* var_52{};
    wp::vec_t<3, wp::float32> var_53{};
    wp::float32 var_54{};
    wp::float32 var_55{};
    wp::float32 var_56{};
    wp::vec_t<3, wp::float32> var_57{};
    const wp::int32 var_58 = 3;
    wp::int32 var_59{};
    wp::float32* var_60{};
    const wp::int32 var_61 = 0;
    wp::float32 var_62{};
    wp::float32 var_63{};
    wp::float32 var_64{};
    const wp::int32 var_65 = 3;
    wp::int32 var_66{};
    const wp::int32 var_67 = 4;
    wp::int32 var_68{};
    wp::float32* var_69{};
    const wp::int32 var_70 = 1;
    wp::float32 var_71{};
    wp::float32 var_72{};
    wp::float32 var_73{};
    const wp::int32 var_74 = 4;
    wp::int32 var_75{};
    const wp::int32 var_76 = 5;
    wp::int32 var_77{};
    wp::float32* var_78{};
    const wp::int32 var_79 = 2;
    wp::float32 var_80{};
    wp::float32 var_81{};
    wp::float32 var_82{};
    const wp::int32 var_83 = 5;
    wp::int32 var_84{};
    const wp::int32 var_85 = 0;
    wp::int32 var_86{};
    wp::float32* var_87{};
    const wp::int32 var_88 = 1;
    wp::int32 var_89{};
    wp::float32* var_90{};
    const wp::int32 var_91 = 2;
    wp::int32 var_92{};
    wp::float32* var_93{};
    wp::vec_t<3, wp::float32> var_94{};
    wp::float32 var_95{};
    wp::float32 var_96{};
    wp::float32 var_97{};
    const wp::int32 var_98 = 3;
    wp::int32 var_99{};
    wp::float32* var_100{};
    const wp::int32 var_101 = 4;
    wp::int32 var_102{};
    wp::float32* var_103{};
    const wp::int32 var_104 = 5;
    wp::int32 var_105{};
    wp::float32* var_106{};
    wp::vec_t<3, wp::float32> var_107{};
    wp::float32 var_108{};
    wp::float32 var_109{};
    wp::float32 var_110{};
    wp::vec_t<3, wp::float32> var_111{};
    wp::vec_t<3, wp::float32> var_112{};
    const wp::int32 var_113 = 0;
    wp::float32 var_114{};
    const wp::int32 var_115 = 0;
    wp::int32 var_116{};
    const wp::int32 var_117 = 1;
    wp::float32 var_118{};
    const wp::int32 var_119 = 1;
    wp::int32 var_120{};
    const wp::int32 var_121 = 2;
    wp::float32 var_122{};
    const wp::int32 var_123 = 2;
    wp::int32 var_124{};
    const wp::int32 var_125 = 0;
    wp::float32 var_126{};
    const wp::int32 var_127 = 3;
    wp::int32 var_128{};
    const wp::int32 var_129 = 1;
    wp::float32 var_130{};
    const wp::int32 var_131 = 4;
    wp::int32 var_132{};
    const wp::int32 var_133 = 2;
    wp::float32 var_134{};
    const wp::int32 var_135 = 5;
    wp::int32 var_136{};
    wp::range_t var_137{};
    wp::int32 var_138{};
    wp::float32* var_139{};
    wp::float32 var_140{};
    wp::float32 var_141{};
    //---------
    // forward
    // def convert_free_distance_joint_f_internal_to_public(                                  <L 1216>
    // joint_id = wp.tid()                                                                    <L 1257>
    var_0 = builtin_tid1d();
    // if articulation_mask:                                                                  <L 1259>
    if (var_articulation_mask) {
        // if not articulation_mask[joint_articulation[joint_id]]:                            <L 1260>
        var_1 = wp::address(var_joint_articulation, var_0);
        var_2 = wp::load(var_1);
        var_3 = wp::address(var_articulation_mask, var_2);
        var_5 = wp::load(var_3);
        var_4 = wp::unot(var_5);
        if (var_4) {
            // return                                                                         <L 1261>
            return;
        }
    }
    // jtype = joint_type[joint_id]                                                           <L 1263>
    var_6 = wp::address(var_joint_type, var_0);
    var_8 = wp::load(var_6);
    var_7 = wp::copy(var_8);
    // qd_start = joint_qd_start[joint_id]                                                    <L 1264>
    var_9 = wp::address(var_joint_qd_start, var_0);
    var_11 = wp::load(var_9);
    var_10 = wp::copy(var_11);
    // qd_end = joint_qd_start[joint_id + 1]                                                  <L 1265>
    var_13 = wp::add(var_0, var_12);
    var_14 = wp::address(var_joint_qd_start, var_13);
    var_16 = wp::load(var_14);
    var_15 = wp::copy(var_16);
    // if jtype == JointType.FREE or jtype == JointType.DISTANCE:                             <L 1267>
    var_19 = (var_7 == var_18);
    var_17 = var_19;
    if (!var_17) {
        var_21 = (var_7 == var_20);
        var_17 = var_17 || var_21;
    }
    if (var_17) {
        // parent = joint_parent[joint_id]                                                    <L 1268>
        var_22 = wp::address(var_joint_parent, var_0);
        var_24 = wp::load(var_22);
        var_23 = wp::copy(var_24);
        // child = joint_child[joint_id]                                                      <L 1269>
        var_25 = wp::address(var_joint_child, var_0);
        var_27 = wp::load(var_25);
        var_26 = wp::copy(var_27);
        // X_wpj = joint_X_p[joint_id]                                                        <L 1274>
        var_28 = wp::address(var_joint_X_p, var_0);
        var_30 = wp::load(var_28);
        var_29 = wp::copy(var_30);
        // if parent >= 0:                                                                    <L 1275>
        var_32 = (var_23 >= var_31);
        if (var_32) {
            // X_wpj = body_q[parent] * X_wpj                                                 <L 1276>
            var_33 = wp::address(var_body_q, var_23);
            var_35 = wp::load(var_33);
            var_34 = wp::mul(var_35, var_29);
        }
        var_36 = wp::where(var_32, var_34, var_29);
        // q_p = wp.transform_get_rotation(X_wpj)                                             <L 1277>
        var_37 = wp::transform_get_rotation(var_36);
        // x_anchor_world = wp.transform_get_translation(X_wpj)                               <L 1278>
        var_38 = wp::transform_get_translation(var_36);
        // x_child_com_world = wp.transform_get_translation(body_q_com[child])                <L 1279>
        var_39 = wp::address(var_body_q_com, var_26);
        var_41 = wp::load(var_39);
        var_40 = wp::transform_get_translation(var_41);
        // r_child_com_parent = wp.quat_rotate_inv(q_p, x_child_com_world - x_anchor_world)       <L 1280>
        var_42 = wp::sub(var_40, var_38);
        var_43 = wp::quat_rotate_inv(var_37, var_42);
        // F_linear = wp.vec3(                                                                <L 1283>
        // joint_f[qd_start + 0],                                                             <L 1284>
        var_45 = wp::add(var_10, var_44);
        var_46 = wp::address(var_joint_f, var_45);
        // joint_f[qd_start + 1],                                                             <L 1285>
        var_48 = wp::add(var_10, var_47);
        var_49 = wp::address(var_joint_f, var_48);
        // joint_f[qd_start + 2],                                                             <L 1286>
        var_51 = wp::add(var_10, var_50);
        var_52 = wp::address(var_joint_f, var_51);
        var_54 = wp::load(var_46);
        var_55 = wp::load(var_49);
        var_56 = wp::load(var_52);
        var_53 = wp::vec_t<3, wp::float32>(var_54, var_55, var_56);
        // shift = wp.cross(r_child_com_parent, F_linear)                                     <L 1288>
        var_57 = wp::cross(var_43, var_53);
        // joint_f[qd_start + 3] = joint_f[qd_start + 3] - shift[0]                           <L 1289>
        var_59 = wp::add(var_10, var_58);
        var_60 = wp::address(var_joint_f, var_59);
        var_62 = wp::extract(var_57, var_61);
        var_64 = wp::load(var_60);
        var_63 = wp::sub(var_64, var_62);
        var_66 = wp::add(var_10, var_65);
        wp::array_store(var_joint_f, var_66, var_63);
        // joint_f[qd_start + 4] = joint_f[qd_start + 4] - shift[1]                           <L 1290>
        var_68 = wp::add(var_10, var_67);
        var_69 = wp::address(var_joint_f, var_68);
        var_71 = wp::extract(var_57, var_70);
        var_73 = wp::load(var_69);
        var_72 = wp::sub(var_73, var_71);
        var_75 = wp::add(var_10, var_74);
        wp::array_store(var_joint_f, var_75, var_72);
        // joint_f[qd_start + 5] = joint_f[qd_start + 5] - shift[2]                           <L 1291>
        var_77 = wp::add(var_10, var_76);
        var_78 = wp::address(var_joint_f, var_77);
        var_80 = wp::extract(var_57, var_79);
        var_82 = wp::load(var_78);
        var_81 = wp::sub(var_82, var_80);
        var_84 = wp::add(var_10, var_83);
        wp::array_store(var_joint_f, var_84, var_81);
        // f_lin_parent = wp.vec3(joint_f[qd_start + 0], joint_f[qd_start + 1], joint_f[qd_start + 2])       <L 1298>
        var_86 = wp::add(var_10, var_85);
        var_87 = wp::address(var_joint_f, var_86);
        var_89 = wp::add(var_10, var_88);
        var_90 = wp::address(var_joint_f, var_89);
        var_92 = wp::add(var_10, var_91);
        var_93 = wp::address(var_joint_f, var_92);
        var_95 = wp::load(var_87);
        var_96 = wp::load(var_90);
        var_97 = wp::load(var_93);
        var_94 = wp::vec_t<3, wp::float32>(var_95, var_96, var_97);
        // f_ang_parent = wp.vec3(joint_f[qd_start + 3], joint_f[qd_start + 4], joint_f[qd_start + 5])       <L 1299>
        var_99 = wp::add(var_10, var_98);
        var_100 = wp::address(var_joint_f, var_99);
        var_102 = wp::add(var_10, var_101);
        var_103 = wp::address(var_joint_f, var_102);
        var_105 = wp::add(var_10, var_104);
        var_106 = wp::address(var_joint_f, var_105);
        var_108 = wp::load(var_100);
        var_109 = wp::load(var_103);
        var_110 = wp::load(var_106);
        var_107 = wp::vec_t<3, wp::float32>(var_108, var_109, var_110);
        // f_lin_world = wp.quat_rotate(q_p, f_lin_parent)                                    <L 1300>
        var_111 = wp::quat_rotate(var_37, var_94);
        // f_ang_world = wp.quat_rotate(q_p, f_ang_parent)                                    <L 1301>
        var_112 = wp::quat_rotate(var_37, var_107);
        // joint_f[qd_start + 0] = f_lin_world[0]                                             <L 1302>
        var_114 = wp::extract(var_111, var_113);
        var_116 = wp::add(var_10, var_115);
        wp::array_store(var_joint_f, var_116, var_114);
        // joint_f[qd_start + 1] = f_lin_world[1]                                             <L 1303>
        var_118 = wp::extract(var_111, var_117);
        var_120 = wp::add(var_10, var_119);
        wp::array_store(var_joint_f, var_120, var_118);
        // joint_f[qd_start + 2] = f_lin_world[2]                                             <L 1304>
        var_122 = wp::extract(var_111, var_121);
        var_124 = wp::add(var_10, var_123);
        wp::array_store(var_joint_f, var_124, var_122);
        // joint_f[qd_start + 3] = f_ang_world[0]                                             <L 1305>
        var_126 = wp::extract(var_112, var_125);
        var_128 = wp::add(var_10, var_127);
        wp::array_store(var_joint_f, var_128, var_126);
        // joint_f[qd_start + 4] = f_ang_world[1]                                             <L 1306>
        var_130 = wp::extract(var_112, var_129);
        var_132 = wp::add(var_10, var_131);
        wp::array_store(var_joint_f, var_132, var_130);
        // joint_f[qd_start + 5] = f_ang_world[2]                                             <L 1307>
        var_134 = wp::extract(var_112, var_133);
        var_136 = wp::add(var_10, var_135);
        wp::array_store(var_joint_f, var_136, var_134);
    }
    // for i in range(qd_start, qd_end):                                                      <L 1313>
    var_137 = wp::range(var_10, var_15);
    start_for_1:;
        if (iter_cmp(var_137) == 0) goto end_for_1;
        var_138 = wp::iter_next(var_137);
        // joint_f[i] = -joint_f[i]                                                           <L 1314>
        var_139 = wp::address(var_joint_f, var_138);
        var_141 = wp::load(var_139);
        var_140 = wp::neg(var_141);
        wp::array_store(var_joint_f, var_138, var_140);
        goto start_for_1;
    end_for_1:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void convert_free_distance_joint_f_internal_to_public_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_f_internal_to_public *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        convert_free_distance_joint_f_internal_to_public_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void convert_free_distance_joint_f_internal_to_public_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_f_internal_to_public *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_type.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_parent.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_articulation.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_mask.strides[0] == (int)sizeof(bool)) & (_wp_args->joint_X_p.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_q.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_q_com.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->joint_f.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        convert_free_distance_joint_f_internal_to_public_cpu_forward_tasks<true>(dim, _wp_args);
    else
        convert_free_distance_joint_f_internal_to_public_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void convert_free_distance_joint_f_internal_to_public_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_f_internal_to_public *_wp_args)
{
    convert_free_distance_joint_f_internal_to_public_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_articulation_fk_with_velocity_conversion {
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::int32 articulation_count;
    wp::array_t<bool> articulation_mask;
    wp::array_t<wp::int32> articulation_indices;
    wp::array_t<wp::float32> joint_q;
    wp::array_t<wp::float32> joint_qd;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_c;
    wp::array_t<wp::vec_t<3, wp::float32>> joint_axis;
    wp::array_t<wp::int32> joint_dof_dim;
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::vec_t<6, wp::float32>> body_qd;
};


template <bool wp_linear_contiguous>
static void eval_articulation_fk_with_velocity_conversion_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_articulation_fk_with_velocity_conversion *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;

    //---------
    // argument vars
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::int32 var_articulation_count = _wp_args->articulation_count;
    wp::array_t<bool> var_articulation_mask = _wp_args->articulation_mask;
    wp::array_t<wp::int32> var_articulation_indices = _wp_args->articulation_indices;
    wp::array_t<wp::float32> var_joint_q = _wp_args->joint_q;
    wp::array_t<wp::float32> var_joint_qd = _wp_args->joint_qd;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c = _wp_args->joint_X_c;
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis = _wp_args->joint_axis;
    wp::array_t<wp::int32> var_joint_dof_dim = _wp_args->joint_dof_dim;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd = _wp_args->body_qd;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32 var_4{};
    wp::int32 var_5{};
    bool var_6{};
    const wp::int32 var_7 = 0;
    bool var_8{};
    bool var_9{};
    bool* var_10;
    bool var_11{};
    bool var_12{};
    wp::int32* var_13{};
    wp::int32 var_14{};
    wp::int32 var_15{};
    wp::int32* var_16{};
    wp::int32 var_17{};
    wp::int32 var_18{};
    //---------
    // forward
    // def eval_articulation_fk_with_velocity_conversion(                                     <L 2162>
    // tid = wp.tid()                                                                         <L 2186>
    var_0 = builtin_tid1d();
    // if articulation_indices:                                                               <L 2189>
    if (var_articulation_indices) {
        // articulation_id = articulation_indices[tid]                                        <L 2191>
        var_1 = wp::address(var_articulation_indices, var_0);
        var_3 = wp::load(var_1);
        var_2 = wp::copy(var_3);
    }
    if (!var_articulation_indices) {
        // articulation_id = tid                                                              <L 2194>
        var_4 = wp::copy(var_0);
    }
    var_5 = wp::where(var_articulation_indices, var_2, var_4);
    // if articulation_id < 0 or articulation_id >= articulation_count:                       <L 2197>
    var_8 = (var_5 < var_7);
    var_6 = var_8;
    if (!var_6) {
        var_9 = (var_5 >= var_articulation_count);
        var_6 = var_6 || var_9;
    }
    if (var_6) {
        // return  # Invalid articulation index                                               <L 2198>
        return;
    }
    // if articulation_mask:                                                                  <L 2201>
    if (var_articulation_mask) {
        // if not articulation_mask[articulation_id]:                                         <L 2202>
        var_10 = wp::address(var_articulation_mask, var_5);
        var_12 = wp::load(var_10);
        var_11 = wp::unot(var_12);
        if (var_11) {
            // return                                                                         <L 2203>
            return;
        }
    }
    // joint_start = articulation_start[articulation_id]                                      <L 2205>
    var_13 = wp::address(var_articulation_start, var_5);
    var_15 = wp::load(var_13);
    var_14 = wp::copy(var_15);
    // joint_end = articulation_end[articulation_id]                                          <L 2206>
    var_16 = wp::address(var_articulation_end, var_5);
    var_18 = wp::load(var_16);
    var_17 = wp::copy(var_18);
    // eval_single_articulation_fk_with_velocity_conversion(                                  <L 2208>
    // joint_start,                                                                           <L 2209>
    // joint_end,                                                                             <L 2210>
    // joint_q,                                                                               <L 2211>
    // joint_qd,                                                                              <L 2212>
    // joint_q_start,                                                                         <L 2213>
    // joint_qd_start,                                                                        <L 2214>
    // joint_type,                                                                            <L 2215>
    // joint_parent,                                                                          <L 2216>
    // joint_child,                                                                           <L 2217>
    // joint_X_p,                                                                             <L 2218>
    // joint_X_c,                                                                             <L 2219>
    // joint_axis,                                                                            <L 2220>
    // joint_dof_dim,                                                                         <L 2221>
    // body_com,                                                                              <L 2222>
    // body_q,                                                                                <L 2224>
    // body_qd,                                                                               <L 2225>
    eval_single_articulation_fk_with_velocity_conversion_0(var_14, var_17, var_joint_q, var_joint_qd, var_joint_q_start, var_joint_qd_start, var_joint_type, var_joint_parent, var_joint_child, var_joint_X_p, var_joint_X_c, var_joint_axis, var_joint_dof_dim, var_body_com, var_body_q, var_body_qd);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_articulation_fk_with_velocity_conversion_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_articulation_fk_with_velocity_conversion *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_articulation_fk_with_velocity_conversion_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_articulation_fk_with_velocity_conversion_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_articulation_fk_with_velocity_conversion *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    eval_articulation_fk_with_velocity_conversion_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_articulation_fk_with_velocity_conversion_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_articulation_fk_with_velocity_conversion *_wp_args)
{
    eval_articulation_fk_with_velocity_conversion_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_zero_kinematic_joint_qdd {
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> body_flags;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::float32> joint_qdd;
};


template <bool wp_linear_contiguous>
static void zero_kinematic_joint_qdd_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_zero_kinematic_joint_qdd *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_zero_kinematic_joint_qdd wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.body_flags.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qdd.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_body_flags = _wp_args->body_flags;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::float32> var_joint_qdd = _wp_args->joint_qdd;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    const wp::int32 var_5 = 2;
    wp::int32 var_6{};
    wp::int32 var_7{};
    const wp::int32 var_8 = 0;
    bool var_9{};
    wp::int32* var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    const wp::int32 var_13 = 1;
    wp::int32 var_14{};
    wp::int32* var_15{};
    wp::int32 var_16{};
    wp::int32 var_17{};
    wp::range_t var_18{};
    wp::int32 var_19{};
    const wp::float32 var_20 = 0.0;
    //---------
    // forward
    // def zero_kinematic_joint_qdd(                                                          <L 1942>
    // joint_id = wp.tid()                                                                    <L 1949>
    var_0 = builtin_tid1d();
    // child = joint_child[joint_id]                                                          <L 1950>
    var_1 = wp::address(var_joint_child, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // if (body_flags[child] & BodyFlags.KINEMATIC) == 0:                                     <L 1951>
    var_4 = wp::address(var_body_flags, var_2);
    var_7 = wp::load(var_4);
    var_6 = wp::bit_and(var_7, var_5);
    var_9 = (var_6 == var_8);
    if (var_9) {
        // return                                                                             <L 1952>
        return;
    }
    // dof_start = joint_qd_start[joint_id]                                                   <L 1954>
    var_10 = wp::address(var_joint_qd_start, var_0);
    var_12 = wp::load(var_10);
    var_11 = wp::copy(var_12);
    // dof_end = joint_qd_start[joint_id + 1]                                                 <L 1955>
    var_14 = wp::add(var_0, var_13);
    var_15 = wp::address(var_joint_qd_start, var_14);
    var_17 = wp::load(var_15);
    var_16 = wp::copy(var_17);
    // for i in range(dof_start, dof_end):                                                    <L 1956>
    var_18 = wp::range(var_11, var_16);
    start_for_1:;
        if (iter_cmp(var_18) == 0) goto end_for_1;
        var_19 = wp::iter_next(var_18);
        // joint_qdd[i] = 0.0                                                                 <L 1957>
        wp::array_store(var_joint_qdd, var_19, var_20);
        goto start_for_1;
    end_for_1:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void zero_kinematic_joint_qdd_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_zero_kinematic_joint_qdd *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        zero_kinematic_joint_qdd_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void zero_kinematic_joint_qdd_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_zero_kinematic_joint_qdd *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->body_flags.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qdd.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        zero_kinematic_joint_qdd_cpu_forward_tasks<true>(dim, _wp_args);
    else
        zero_kinematic_joint_qdd_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void zero_kinematic_joint_qdd_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_zero_kinematic_joint_qdd *_wp_args)
{
    zero_kinematic_joint_qdd_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_convert_free_distance_joint_qd_public_to_internal {
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::float32> joint_qd_public;
    wp::array_t<wp::float32> joint_qd_internal;
};


template <bool wp_linear_contiguous>
static void convert_free_distance_joint_qd_public_to_internal_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_convert_free_distance_joint_qd_public_to_internal *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_convert_free_distance_joint_qd_public_to_internal wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_type.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_parent.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_X_p.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_q.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_com.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.joint_qd_public.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.joint_qd_internal.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::float32> var_joint_qd_public = _wp_args->joint_qd_public;
    wp::array_t<wp::float32> var_joint_qd_internal = _wp_args->joint_qd_internal;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    const wp::int32 var_4 = 1;
    wp::int32 var_5{};
    wp::int32* var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32* var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    bool var_12{};
    const wp::int32 var_13 = 4;
    bool var_14{};
    const wp::int32 var_15 = 5;
    bool var_16{};
    wp::range_t var_17{};
    wp::int32 var_18{};
    wp::float32* var_19{};
    wp::float32 var_20{};
    wp::float32 var_21{};
    wp::int32* var_22{};
    wp::int32 var_23{};
    wp::int32 var_24{};
    wp::int32* var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    wp::transform_t<wp::float32>* var_28{};
    wp::transform_t<wp::float32> var_29{};
    wp::transform_t<wp::float32> var_30{};
    const wp::int32 var_31 = 0;
    bool var_32{};
    wp::transform_t<wp::float32>* var_33{};
    wp::transform_t<wp::float32> var_34{};
    wp::transform_t<wp::float32> var_35{};
    wp::transform_t<wp::float32> var_36{};
    wp::quat_t<wp::float32> var_37{};
    wp::vec_t<3, wp::float32> var_38{};
    wp::transform_t<wp::float32>* var_39{};
    wp::vec_t<3, wp::float32>* var_40{};
    wp::vec_t<3, wp::float32> var_41{};
    wp::transform_t<wp::float32> var_42{};
    wp::vec_t<3, wp::float32> var_43{};
    wp::vec_t<3, wp::float32> var_44{};
    wp::vec_t<3, wp::float32> var_45{};
    const wp::int32 var_46 = 0;
    wp::int32 var_47{};
    wp::float32* var_48{};
    const wp::int32 var_49 = 1;
    wp::int32 var_50{};
    wp::float32* var_51{};
    const wp::int32 var_52 = 2;
    wp::int32 var_53{};
    wp::float32* var_54{};
    wp::vec_t<3, wp::float32> var_55{};
    wp::float32 var_56{};
    wp::float32 var_57{};
    wp::float32 var_58{};
    const wp::int32 var_59 = 3;
    wp::int32 var_60{};
    wp::float32* var_61{};
    const wp::int32 var_62 = 4;
    wp::int32 var_63{};
    wp::float32* var_64{};
    const wp::int32 var_65 = 5;
    wp::int32 var_66{};
    wp::float32* var_67{};
    wp::vec_t<3, wp::float32> var_68{};
    wp::float32 var_69{};
    wp::float32 var_70{};
    wp::float32 var_71{};
    wp::vec_t<3, wp::float32> var_72{};
    wp::vec_t<3, wp::float32> var_73{};
    const wp::int32 var_74 = 0;
    wp::float32 var_75{};
    const wp::int32 var_76 = 0;
    wp::int32 var_77{};
    const wp::int32 var_78 = 1;
    wp::float32 var_79{};
    const wp::int32 var_80 = 1;
    wp::int32 var_81{};
    const wp::int32 var_82 = 2;
    wp::float32 var_83{};
    const wp::int32 var_84 = 2;
    wp::int32 var_85{};
    const wp::int32 var_86 = 0;
    wp::float32 var_87{};
    const wp::int32 var_88 = 3;
    wp::int32 var_89{};
    const wp::int32 var_90 = 1;
    wp::float32 var_91{};
    const wp::int32 var_92 = 4;
    wp::int32 var_93{};
    const wp::int32 var_94 = 2;
    wp::float32 var_95{};
    const wp::int32 var_96 = 5;
    wp::int32 var_97{};
    //---------
    // forward
    // def convert_free_distance_joint_qd_public_to_internal(                                 <L 1049>
    // joint_id = wp.tid()                                                                    <L 1060>
    var_0 = builtin_tid1d();
    // qd_start = joint_qd_start[joint_id]                                                    <L 1061>
    var_1 = wp::address(var_joint_qd_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // qd_end = joint_qd_start[joint_id + 1]                                                  <L 1062>
    var_5 = wp::add(var_0, var_4);
    var_6 = wp::address(var_joint_qd_start, var_5);
    var_8 = wp::load(var_6);
    var_7 = wp::copy(var_8);
    // jtype = joint_type[joint_id]                                                           <L 1063>
    var_9 = wp::address(var_joint_type, var_0);
    var_11 = wp::load(var_9);
    var_10 = wp::copy(var_11);
    // if jtype != JointType.FREE and jtype != JointType.DISTANCE:                            <L 1065>
    var_14 = (var_10 != var_13);
    var_12 = var_14;
    if (var_12) {
        var_16 = (var_10 != var_15);
        var_12 = var_12 && var_16;
    }
    if (var_12) {
        // for i in range(qd_start, qd_end):                                                  <L 1066>
        var_17 = wp::range(var_2, var_7);
        start_for_0:;
            if (iter_cmp(var_17) == 0) goto end_for_0;
            var_18 = wp::iter_next(var_17);
            // joint_qd_internal[i] = joint_qd_public[i]                                      <L 1067>
            var_19 = wp::address(var_joint_qd_public, var_18);
            var_21 = wp::load(var_19);
            var_20 = wp::copy(var_21);
            wp::array_store(var_joint_qd_internal, var_18, var_20);
            goto start_for_0;
        end_for_0:;
        // return                                                                             <L 1068>
        return;
    }
    // parent = joint_parent[joint_id]                                                        <L 1070>
    var_22 = wp::address(var_joint_parent, var_0);
    var_24 = wp::load(var_22);
    var_23 = wp::copy(var_24);
    // child = joint_child[joint_id]                                                          <L 1071>
    var_25 = wp::address(var_joint_child, var_0);
    var_27 = wp::load(var_25);
    var_26 = wp::copy(var_27);
    // X_wpj = joint_X_p[joint_id]                                                            <L 1073>
    var_28 = wp::address(var_joint_X_p, var_0);
    var_30 = wp::load(var_28);
    var_29 = wp::copy(var_30);
    // if parent >= 0:                                                                        <L 1074>
    var_32 = (var_23 >= var_31);
    if (var_32) {
        // X_wpj = body_q[parent] * X_wpj                                                     <L 1075>
        var_33 = wp::address(var_body_q, var_23);
        var_35 = wp::load(var_33);
        var_34 = wp::mul(var_35, var_29);
    }
    var_36 = wp::where(var_32, var_34, var_29);
    // q_p = wp.transform_get_rotation(X_wpj)                                                 <L 1077>
    var_37 = wp::transform_get_rotation(var_36);
    // x_anchor_world = wp.transform_get_translation(X_wpj)                                   <L 1078>
    var_38 = wp::transform_get_translation(var_36);
    // x_child_com_world = wp.transform_point(body_q[child], body_com[child])                 <L 1079>
    var_39 = wp::address(var_body_q, var_26);
    var_40 = wp::address(var_body_com, var_26);
    var_42 = wp::load(var_39);
    var_43 = wp::load(var_40);
    var_41 = wp::transform_point(var_42, var_43);
    // r_child_com_parent = wp.quat_rotate_inv(q_p, x_child_com_world - x_anchor_world)       <L 1080>
    var_44 = wp::sub(var_41, var_38);
    var_45 = wp::quat_rotate_inv(var_37, var_44);
    // v_com_parent = wp.vec3(                                                                <L 1082>
    // joint_qd_public[qd_start + 0],                                                         <L 1083>
    var_47 = wp::add(var_2, var_46);
    var_48 = wp::address(var_joint_qd_public, var_47);
    // joint_qd_public[qd_start + 1],                                                         <L 1084>
    var_50 = wp::add(var_2, var_49);
    var_51 = wp::address(var_joint_qd_public, var_50);
    // joint_qd_public[qd_start + 2],                                                         <L 1085>
    var_53 = wp::add(var_2, var_52);
    var_54 = wp::address(var_joint_qd_public, var_53);
    var_56 = wp::load(var_48);
    var_57 = wp::load(var_51);
    var_58 = wp::load(var_54);
    var_55 = wp::vec_t<3, wp::float32>(var_56, var_57, var_58);
    // omega_parent = wp.vec3(                                                                <L 1087>
    // joint_qd_public[qd_start + 3],                                                         <L 1088>
    var_60 = wp::add(var_2, var_59);
    var_61 = wp::address(var_joint_qd_public, var_60);
    // joint_qd_public[qd_start + 4],                                                         <L 1089>
    var_63 = wp::add(var_2, var_62);
    var_64 = wp::address(var_joint_qd_public, var_63);
    // joint_qd_public[qd_start + 5],                                                         <L 1090>
    var_66 = wp::add(var_2, var_65);
    var_67 = wp::address(var_joint_qd_public, var_66);
    var_69 = wp::load(var_61);
    var_70 = wp::load(var_64);
    var_71 = wp::load(var_67);
    var_68 = wp::vec_t<3, wp::float32>(var_69, var_70, var_71);
    // v_internal_parent = v_com_parent - wp.cross(omega_parent, r_child_com_parent)          <L 1092>
    var_72 = wp::cross(var_68, var_45);
    var_73 = wp::sub(var_55, var_72);
    // joint_qd_internal[qd_start + 0] = v_internal_parent[0]                                 <L 1094>
    var_75 = wp::extract(var_73, var_74);
    var_77 = wp::add(var_2, var_76);
    wp::array_store(var_joint_qd_internal, var_77, var_75);
    // joint_qd_internal[qd_start + 1] = v_internal_parent[1]                                 <L 1095>
    var_79 = wp::extract(var_73, var_78);
    var_81 = wp::add(var_2, var_80);
    wp::array_store(var_joint_qd_internal, var_81, var_79);
    // joint_qd_internal[qd_start + 2] = v_internal_parent[2]                                 <L 1096>
    var_83 = wp::extract(var_73, var_82);
    var_85 = wp::add(var_2, var_84);
    wp::array_store(var_joint_qd_internal, var_85, var_83);
    // joint_qd_internal[qd_start + 3] = omega_parent[0]                                      <L 1097>
    var_87 = wp::extract(var_68, var_86);
    var_89 = wp::add(var_2, var_88);
    wp::array_store(var_joint_qd_internal, var_89, var_87);
    // joint_qd_internal[qd_start + 4] = omega_parent[1]                                      <L 1098>
    var_91 = wp::extract(var_68, var_90);
    var_93 = wp::add(var_2, var_92);
    wp::array_store(var_joint_qd_internal, var_93, var_91);
    // joint_qd_internal[qd_start + 5] = omega_parent[2]                                      <L 1099>
    var_95 = wp::extract(var_68, var_94);
    var_97 = wp::add(var_2, var_96);
    wp::array_store(var_joint_qd_internal, var_97, var_95);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void convert_free_distance_joint_qd_public_to_internal_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_qd_public_to_internal *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        convert_free_distance_joint_qd_public_to_internal_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void convert_free_distance_joint_qd_public_to_internal_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_qd_public_to_internal *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_type.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_parent.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_X_p.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_q.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_com.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->joint_qd_public.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->joint_qd_internal.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        convert_free_distance_joint_qd_public_to_internal_cpu_forward_tasks<true>(dim, _wp_args);
    else
        convert_free_distance_joint_qd_public_to_internal_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void convert_free_distance_joint_qd_public_to_internal_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_free_distance_joint_qd_public_to_internal *_wp_args)
{
    convert_free_distance_joint_qd_public_to_internal_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_rigid_mass {
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> articulation_M_start;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> body_I_s;
    wp::array_t<wp::float32> M;
};


template <bool wp_linear_contiguous>
static void eval_rigid_mass_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_rigid_mass *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_eval_rigid_mass wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.articulation_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_end.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_M_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.body_I_s.strides[0] = sizeof(wp::mat_t<6, 6, wp::float32>);
        wp_contiguous_args.M.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_articulation_M_start = _wp_args->articulation_M_start;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_s = _wp_args->body_I_s;
    wp::array_t<wp::float32> var_M = _wp_args->M;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32 var_7{};
    wp::int32* var_8{};
    wp::int32 var_9{};
    wp::int32 var_10{};
    //---------
    // forward
    // def eval_rigid_mass(                                                                   <L 1563>
    // index = wp.tid()                                                                       <L 1572>
    var_0 = builtin_tid1d();
    // joint_start = articulation_start[index]                                                <L 1574>
    var_1 = wp::address(var_articulation_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // joint_end = articulation_end[index]                                                    <L 1575>
    var_4 = wp::address(var_articulation_end, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // joint_count = joint_end - joint_start                                                  <L 1576>
    var_7 = wp::sub(var_5, var_2);
    // M_offset = articulation_M_start[index]                                                 <L 1578>
    var_8 = wp::address(var_articulation_M_start, var_0);
    var_10 = wp::load(var_8);
    var_9 = wp::copy(var_10);
    // spatial_mass(body_I_s, joint_start, joint_count, M_offset, M)                          <L 1580>
    spatial_mass_0(var_body_I_s, var_2, var_7, var_9, var_M);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_rigid_mass_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_mass *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_rigid_mass_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_rigid_mass_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_mass *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->articulation_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_end.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_M_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->body_I_s.strides[0] == (int)sizeof(wp::mat_t<6, 6, wp::float32>)) & (_wp_args->M.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        eval_rigid_mass_cpu_forward_tasks<true>(dim, _wp_args);
    else
        eval_rigid_mass_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_rigid_mass_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_mass *_wp_args)
{
    eval_rigid_mass_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_convert_body_force_com_to_origin {
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::transform_t<wp::float32>> body_X_com;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f_ext;
};


template <bool wp_linear_contiguous>
static void convert_body_force_com_to_origin_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_convert_body_force_com_to_origin *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_convert_body_force_com_to_origin wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.body_q.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_X_com.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_f_ext.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::transform_t<wp::float32>> var_body_X_com = _wp_args->body_X_com;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_ext = _wp_args->body_f_ext;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::vec_t<6, wp::float32>* var_1{};
    wp::vec_t<6, wp::float32> var_2{};
    wp::vec_t<6, wp::float32> var_3{};
    wp::float32 var_4{};
    const wp::float32 var_5 = 0.0;
    bool var_6{};
    wp::transform_t<wp::float32>* var_7{};
    wp::transform_t<wp::float32>* var_8{};
    wp::transform_t<wp::float32> var_9{};
    wp::transform_t<wp::float32> var_10{};
    wp::transform_t<wp::float32> var_11{};
    wp::vec_t<3, wp::float32> var_12{};
    wp::vec_t<3, wp::float32> var_13{};
    wp::vec_t<3, wp::float32> var_14{};
    wp::vec_t<3, wp::float32> var_15{};
    wp::vec_t<3, wp::float32> var_16{};
    wp::vec_t<6, wp::float32> var_17{};
    wp::vec_t<6, wp::float32> var_18{};
    //---------
    // forward
    // def convert_body_force_com_to_origin(                                                  <L 994>
    // tid = wp.tid()                                                                         <L 1000>
    var_0 = builtin_tid1d();
    // f_ext_com = body_f_ext[tid]                                                            <L 1002>
    var_1 = wp::address(var_body_f_ext, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // if wp.length(f_ext_com) == 0.0:                                                        <L 1005>
    var_4 = wp::length(var_2);
    var_6 = (var_4 == var_5);
    if (var_6) {
        // return                                                                             <L 1006>
        return;
    }
    // body_q_com_val = body_q[tid] * body_X_com[tid]                                         <L 1008>
    var_7 = wp::address(var_body_q, var_0);
    var_8 = wp::address(var_body_X_com, var_0);
    var_10 = wp::load(var_7);
    var_11 = wp::load(var_8);
    var_9 = wp::mul(var_10, var_11);
    // r_com = wp.transform_get_translation(body_q_com_val)                                   <L 1009>
    var_12 = wp::transform_get_translation(var_9);
    // force = wp.spatial_top(f_ext_com)                                                      <L 1011>
    var_13 = wp::spatial_top(var_2);
    // torque_com = wp.spatial_bottom(f_ext_com)                                              <L 1012>
    var_14 = wp::spatial_bottom(var_2);
    // body_f_ext[tid] = -wp.spatial_vector(force, torque_com + wp.cross(r_com, force))       <L 1014>
    var_15 = wp::cross(var_12, var_13);
    var_16 = wp::add(var_14, var_15);
    var_17 = wp::vec_t<6, wp::float32>(var_13, var_16);
    var_18 = wp::neg(var_17);
    wp::array_store(var_body_f_ext, var_0, var_18);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void convert_body_force_com_to_origin_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_body_force_com_to_origin *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        convert_body_force_com_to_origin_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void convert_body_force_com_to_origin_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_body_force_com_to_origin *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->body_q.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_X_com.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_f_ext.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)));
    if (wp_linear_contiguous)
        convert_body_force_com_to_origin_cpu_forward_tasks<true>(dim, _wp_args);
    else
        convert_body_force_com_to_origin_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void convert_body_force_com_to_origin_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_convert_body_force_com_to_origin *_wp_args)
{
    convert_body_force_com_to_origin_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_dense_gemm_batched {
    wp::array_t<wp::int32> m;
    wp::array_t<wp::int32> n;
    wp::array_t<wp::int32> p;
    bool transpose_A;
    bool transpose_B;
    wp::array_t<wp::int32> A_start;
    wp::array_t<wp::int32> B_start;
    wp::array_t<wp::int32> C_start;
    wp::array_t<wp::float32> A;
    wp::array_t<wp::float32> B;
    wp::array_t<wp::float32> C;
};


template <bool wp_linear_contiguous>
static void eval_dense_gemm_batched_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_dense_gemm_batched *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_eval_dense_gemm_batched wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.m.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.n.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.p.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.A_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.B_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.C_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.A.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.B.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.C.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_m = _wp_args->m;
    wp::array_t<wp::int32> var_n = _wp_args->n;
    wp::array_t<wp::int32> var_p = _wp_args->p;
    bool var_transpose_A = _wp_args->transpose_A;
    bool var_transpose_B = _wp_args->transpose_B;
    wp::array_t<wp::int32> var_A_start = _wp_args->A_start;
    wp::array_t<wp::int32> var_B_start = _wp_args->B_start;
    wp::array_t<wp::int32> var_C_start = _wp_args->C_start;
    wp::array_t<wp::float32> var_A = _wp_args->A;
    wp::array_t<wp::float32> var_B = _wp_args->B;
    wp::array_t<wp::float32> var_C = _wp_args->C;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32* var_2{};
    wp::int32* var_3{};
    wp::int32* var_4{};
    wp::int32* var_5{};
    wp::int32* var_6{};
    wp::int32 var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    //---------
    // forward
    // def eval_dense_gemm_batched(                                                           <L 1667>
    // batch = wp.tid()  # /kNumThreadsPerBlock;                                              <L 1682>
    var_0 = builtin_tid1d();
    // dense_gemm(                                                                            <L 1684>
    // m[batch],                                                                              <L 1685>
    var_1 = wp::address(var_m, var_0);
    // n[batch],                                                                              <L 1686>
    var_2 = wp::address(var_n, var_0);
    // p[batch],                                                                              <L 1687>
    var_3 = wp::address(var_p, var_0);
    // transpose_A,                                                                           <L 1688>
    // transpose_B,                                                                           <L 1689>
    // A_start[batch],                                                                        <L 1690>
    var_4 = wp::address(var_A_start, var_0);
    // B_start[batch],                                                                        <L 1691>
    var_5 = wp::address(var_B_start, var_0);
    // C_start[batch],                                                                        <L 1692>
    var_6 = wp::address(var_C_start, var_0);
    // A,                                                                                     <L 1693>
    // B,                                                                                     <L 1694>
    // C,                                                                                     <L 1695>
    var_7 = wp::load(var_1);
    var_8 = wp::load(var_2);
    var_9 = wp::load(var_3);
    var_10 = wp::load(var_4);
    var_11 = wp::load(var_5);
    var_12 = wp::load(var_6);
    dense_gemm_0(var_7, var_8, var_9, var_transpose_A, var_transpose_B, var_10, var_11, var_12, var_A, var_B, var_C);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_dense_gemm_batched_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_gemm_batched *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_dense_gemm_batched_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_dense_gemm_batched_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_gemm_batched *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->m.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->n.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->p.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->A_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->B_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->C_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->A.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->B.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->C.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        eval_dense_gemm_batched_cpu_forward_tasks<true>(dim, _wp_args);
    else
        eval_dense_gemm_batched_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_dense_gemm_batched_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_gemm_batched *_wp_args)
{
    eval_dense_gemm_batched_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_reduce_mimic_forces {
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_mimic_joint;
    wp::array_t<wp::vec_t<2, wp::float32>> joint_mimic_coeffs;
    wp::array_t<wp::float32> joint_tau;
    wp::array_t<wp::float32> joint_tau_reduced;
};


template <bool wp_linear_contiguous>
static void reduce_mimic_forces_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_reduce_mimic_forces *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_reduce_mimic_forces wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.articulation_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_end.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_mimic_joint.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_mimic_coeffs.strides[0] = sizeof(wp::vec_t<2, wp::float32>);
        wp_contiguous_args.joint_tau.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.joint_tau_reduced.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_mimic_joint = _wp_args->joint_mimic_joint;
    wp::array_t<wp::vec_t<2, wp::float32>> var_joint_mimic_coeffs = _wp_args->joint_mimic_coeffs;
    wp::array_t<wp::float32> var_joint_tau = _wp_args->joint_tau;
    wp::array_t<wp::float32> var_joint_tau_reduced = _wp_args->joint_tau_reduced;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::range_t var_7{};
    wp::int32 var_8{};
    wp::int32* var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    const wp::float32 var_12 = 1.0;
    wp::float32 var_13{};
    wp::int32* var_14{};
    wp::int32 var_15{};
    wp::int32 var_16{};
    const wp::int32 var_17 = 0;
    bool var_18{};
    wp::vec_t<2, wp::float32>* var_19{};
    const wp::int32 var_20 = 1;
    wp::float32 var_21{};
    wp::vec_t<2, wp::float32> var_22{};
    wp::int32* var_23{};
    wp::int32 var_24{};
    wp::int32 var_25{};
    wp::float32 var_26{};
    wp::int32 var_27{};
    const wp::int32 var_28 = 1;
    wp::int32 var_29{};
    wp::int32* var_30{};
    wp::int32* var_31{};
    wp::int32 var_32{};
    wp::int32 var_33{};
    wp::int32 var_34{};
    wp::range_t var_35{};
    wp::int32 var_36{};
    wp::int32 var_37{};
    wp::int32* var_38{};
    wp::int32 var_39{};
    wp::int32 var_40{};
    wp::float32* var_41{};
    wp::float32 var_42{};
    wp::float32 var_43{};
    wp::float32 var_44{};
    //---------
    // forward
    // def reduce_mimic_forces(                                                               <L 136>
    // articulation = wp.tid()                                                                <L 146>
    var_0 = builtin_tid1d();
    // joint_start = articulation_start[articulation]                                         <L 147>
    var_1 = wp::address(var_articulation_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // joint_end = articulation_end[articulation]                                             <L 148>
    var_4 = wp::address(var_articulation_end, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // for joint in range(joint_start, joint_end):                                            <L 150>
    var_7 = wp::range(var_2, var_5);
    start_for_0:;
        if (iter_cmp(var_7) == 0) goto end_for_0;
        var_8 = wp::iter_next(var_7);
        // reference = joint_mimic_joint[joint]                                               <L 151>
        var_9 = wp::address(var_joint_mimic_joint, var_8);
        var_11 = wp::load(var_9);
        var_10 = wp::copy(var_11);
        // multiplier = float(1.0)                                                            <L 152>
        var_13 = wp::float(var_12);
        // destination_start = joint_qd_start[joint]                                          <L 153>
        var_14 = wp::address(var_joint_qd_start, var_8);
        var_16 = wp::load(var_14);
        var_15 = wp::copy(var_16);
        // if reference >= 0:                                                                 <L 154>
        var_18 = (var_10 >= var_17);
        if (var_18) {
            // multiplier = joint_mimic_coeffs[joint][1]                                      <L 155>
            var_19 = wp::address(var_joint_mimic_coeffs, var_8);
            var_22 = wp::load(var_19);
            var_21 = wp::extract(var_22, var_20);
            // destination_start = joint_qd_start[reference]                                  <L 156>
            var_23 = wp::address(var_joint_qd_start, var_10);
            var_25 = wp::load(var_23);
            var_24 = wp::copy(var_25);
        }
        var_26 = wp::where(var_18, var_21, var_13);
        var_27 = wp::where(var_18, var_24, var_15);
        // dof_count = joint_qd_start[joint + 1] - joint_qd_start[joint]                      <L 158>
        var_29 = wp::add(var_8, var_28);
        var_30 = wp::address(var_joint_qd_start, var_29);
        var_31 = wp::address(var_joint_qd_start, var_8);
        var_33 = wp::load(var_30);
        var_34 = wp::load(var_31);
        var_32 = wp::sub(var_33, var_34);
        // for axis in range(dof_count):                                                      <L 159>
        var_35 = wp::range(var_32);
        start_for_2:;
            if (iter_cmp(var_35) == 0) goto end_for_2;
            var_36 = wp::iter_next(var_35);
            // wp.atomic_add(                                                                 <L 160>
            // joint_tau_reduced,                                                             <L 161>
            // destination_start + axis,                                                      <L 162>
            var_37 = wp::add(var_27, var_36);
            // multiplier * joint_tau[joint_qd_start[joint] + axis],                          <L 163>
            var_38 = wp::address(var_joint_qd_start, var_8);
            var_40 = wp::load(var_38);
            var_39 = wp::add(var_40, var_36);
            var_41 = wp::address(var_joint_tau, var_39);
            var_43 = wp::load(var_41);
            var_42 = wp::mul(var_26, var_43);
            var_44 = wp::atomic_add(var_joint_tau_reduced, var_37, var_42);
            goto start_for_2;
        end_for_2:;
        goto start_for_0;
    end_for_0:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void reduce_mimic_forces_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_reduce_mimic_forces *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        reduce_mimic_forces_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void reduce_mimic_forces_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_reduce_mimic_forces *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->articulation_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_end.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_mimic_joint.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_mimic_coeffs.strides[0] == (int)sizeof(wp::vec_t<2, wp::float32>)) & (_wp_args->joint_tau.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->joint_tau_reduced.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        reduce_mimic_forces_cpu_forward_tasks<true>(dim, _wp_args);
    else
        reduce_mimic_forces_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void reduce_mimic_forces_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_reduce_mimic_forces *_wp_args)
{
    reduce_mimic_forces_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_reduce_mimic_inertia {
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> articulation_H_start;
    wp::array_t<wp::int32> articulation_H_rows;
    wp::array_t<wp::int32> articulation_dof_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_mimic_joint;
    wp::array_t<wp::vec_t<2, wp::float32>> joint_mimic_coeffs;
    wp::array_t<wp::float32> joint_armature;
    wp::array_t<wp::float32> H;
    wp::array_t<wp::float32> H_reduced;
};


template <bool wp_linear_contiguous>
static void reduce_mimic_inertia_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_reduce_mimic_inertia *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_reduce_mimic_inertia wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.articulation_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_end.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_H_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_H_rows.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.articulation_dof_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_mimic_joint.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_mimic_coeffs.strides[0] = sizeof(wp::vec_t<2, wp::float32>);
        wp_contiguous_args.joint_armature.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.H.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.H_reduced.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_articulation_H_start = _wp_args->articulation_H_start;
    wp::array_t<wp::int32> var_articulation_H_rows = _wp_args->articulation_H_rows;
    wp::array_t<wp::int32> var_articulation_dof_start = _wp_args->articulation_dof_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_mimic_joint = _wp_args->joint_mimic_joint;
    wp::array_t<wp::vec_t<2, wp::float32>> var_joint_mimic_coeffs = _wp_args->joint_mimic_coeffs;
    wp::array_t<wp::float32> var_joint_armature = _wp_args->joint_armature;
    wp::array_t<wp::float32> var_H = _wp_args->H;
    wp::array_t<wp::float32> var_H_reduced = _wp_args->H_reduced;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32* var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::int32* var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    wp::int32* var_13{};
    wp::int32 var_14{};
    wp::int32 var_15{};
    wp::range_t var_16{};
    wp::int32 var_17{};
    wp::int32* var_18{};
    wp::int32 var_19{};
    wp::int32 var_20{};
    const wp::float32 var_21 = 1.0;
    wp::float32 var_22{};
    wp::int32* var_23{};
    wp::int32 var_24{};
    wp::int32 var_25{};
    const wp::int32 var_26 = 0;
    bool var_27{};
    wp::vec_t<2, wp::float32>* var_28{};
    const wp::int32 var_29 = 1;
    wp::float32 var_30{};
    wp::vec_t<2, wp::float32> var_31{};
    wp::int32* var_32{};
    wp::int32 var_33{};
    wp::int32 var_34{};
    wp::float32 var_35{};
    wp::int32 var_36{};
    const wp::int32 var_37 = 1;
    wp::int32 var_38{};
    wp::int32* var_39{};
    wp::int32* var_40{};
    wp::int32 var_41{};
    wp::int32 var_42{};
    wp::int32 var_43{};
    wp::range_t var_44{};
    wp::int32 var_45{};
    wp::int32* var_46{};
    wp::int32 var_47{};
    wp::int32 var_48{};
    wp::int32 var_49{};
    wp::range_t var_50{};
    wp::int32 var_51{};
    wp::int32* var_52{};
    wp::int32 var_53{};
    wp::int32 var_54{};
    const wp::float32 var_55 = 1.0;
    wp::float32 var_56{};
    wp::int32* var_57{};
    wp::int32 var_58{};
    wp::int32 var_59{};
    const wp::int32 var_60 = 0;
    bool var_61{};
    wp::vec_t<2, wp::float32>* var_62{};
    const wp::int32 var_63 = 1;
    wp::float32 var_64{};
    wp::vec_t<2, wp::float32> var_65{};
    wp::int32* var_66{};
    wp::int32 var_67{};
    wp::int32 var_68{};
    wp::float32 var_69{};
    wp::int32 var_70{};
    const wp::int32 var_71 = 1;
    wp::int32 var_72{};
    wp::int32* var_73{};
    wp::int32* var_74{};
    wp::int32 var_75{};
    wp::int32 var_76{};
    wp::int32 var_77{};
    wp::range_t var_78{};
    wp::int32 var_79{};
    wp::int32* var_80{};
    wp::int32 var_81{};
    wp::int32 var_82{};
    wp::int32 var_83{};
    wp::int32 var_84{};
    wp::int32 var_85{};
    wp::int32 var_86{};
    wp::int32 var_87{};
    wp::int32 var_88{};
    wp::float32* var_89{};
    wp::float32 var_90{};
    wp::float32 var_91{};
    bool var_92{};
    wp::float32* var_93{};
    wp::float32 var_94{};
    wp::float32 var_95{};
    wp::float32 var_96{};
    wp::int32 var_97{};
    wp::int32 var_98{};
    wp::int32 var_99{};
    wp::int32 var_100{};
    wp::int32 var_101{};
    wp::float32 var_102{};
    wp::float32 var_103{};
    wp::float32 var_104{};
    wp::range_t var_105{};
    wp::int32 var_106{};
    wp::int32* var_107{};
    const wp::int32 var_108 = 0;
    bool var_109{};
    wp::int32 var_110{};
    const wp::int32 var_111 = 1;
    wp::int32 var_112{};
    wp::int32* var_113{};
    wp::int32* var_114{};
    wp::int32 var_115{};
    wp::int32 var_116{};
    wp::int32 var_117{};
    wp::range_t var_118{};
    wp::int32 var_119{};
    wp::int32* var_120{};
    wp::int32 var_121{};
    wp::int32 var_122{};
    wp::int32 var_123{};
    wp::int32 var_124{};
    wp::int32 var_125{};
    wp::int32 var_126{};
    const wp::float32 var_127 = 1.0;
    wp::float32 var_128{};
    //---------
    // forward
    // def reduce_mimic_inertia(                                                              <L 69>
    // articulation = wp.tid()                                                                <L 83>
    var_0 = builtin_tid1d();
    // joint_start = articulation_start[articulation]                                         <L 84>
    var_1 = wp::address(var_articulation_start, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // joint_end = articulation_end[articulation]                                             <L 85>
    var_4 = wp::address(var_articulation_end, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // matrix_start = articulation_H_start[articulation]                                      <L 86>
    var_7 = wp::address(var_articulation_H_start, var_0);
    var_9 = wp::load(var_7);
    var_8 = wp::copy(var_9);
    // matrix_rows = articulation_H_rows[articulation]                                        <L 87>
    var_10 = wp::address(var_articulation_H_rows, var_0);
    var_12 = wp::load(var_10);
    var_11 = wp::copy(var_12);
    // dof_start = articulation_dof_start[articulation]                                       <L 88>
    var_13 = wp::address(var_articulation_dof_start, var_0);
    var_15 = wp::load(var_13);
    var_14 = wp::copy(var_15);
    // for row_joint in range(joint_start, joint_end):                                        <L 90>
    var_16 = wp::range(var_2, var_5);
    start_for_0:;
        if (iter_cmp(var_16) == 0) goto end_for_0;
        var_17 = wp::iter_next(var_16);
        // row_reference = joint_mimic_joint[row_joint]                                       <L 91>
        var_18 = wp::address(var_joint_mimic_joint, var_17);
        var_20 = wp::load(var_18);
        var_19 = wp::copy(var_20);
        // row_multiplier = float(1.0)                                                        <L 92>
        var_22 = wp::float(var_21);
        // row_destination_start = joint_qd_start[row_joint]                                  <L 93>
        var_23 = wp::address(var_joint_qd_start, var_17);
        var_25 = wp::load(var_23);
        var_24 = wp::copy(var_25);
        // if row_reference >= 0:                                                             <L 94>
        var_27 = (var_19 >= var_26);
        if (var_27) {
            // row_multiplier = joint_mimic_coeffs[row_joint][1]                              <L 95>
            var_28 = wp::address(var_joint_mimic_coeffs, var_17);
            var_31 = wp::load(var_28);
            var_30 = wp::extract(var_31, var_29);
            // row_destination_start = joint_qd_start[row_reference]                          <L 96>
            var_32 = wp::address(var_joint_qd_start, var_19);
            var_34 = wp::load(var_32);
            var_33 = wp::copy(var_34);
        }
        var_35 = wp::where(var_27, var_30, var_22);
        var_36 = wp::where(var_27, var_33, var_24);
        // row_count = joint_qd_start[row_joint + 1] - joint_qd_start[row_joint]              <L 98>
        var_38 = wp::add(var_17, var_37);
        var_39 = wp::address(var_joint_qd_start, var_38);
        var_40 = wp::address(var_joint_qd_start, var_17);
        var_42 = wp::load(var_39);
        var_43 = wp::load(var_40);
        var_41 = wp::sub(var_42, var_43);
        // for row_axis in range(row_count):                                                  <L 99>
        var_44 = wp::range(var_41);
        start_for_2:;
            if (iter_cmp(var_44) == 0) goto end_for_2;
            var_45 = wp::iter_next(var_44);
            // source_row = joint_qd_start[row_joint] + row_axis                              <L 100>
            var_46 = wp::address(var_joint_qd_start, var_17);
            var_48 = wp::load(var_46);
            var_47 = wp::add(var_48, var_45);
            // destination_row = row_destination_start + row_axis                             <L 101>
            var_49 = wp::add(var_36, var_45);
            // for column_joint in range(joint_start, joint_end):                             <L 103>
            var_50 = wp::range(var_2, var_5);
            start_for_4:;
                if (iter_cmp(var_50) == 0) goto end_for_4;
                var_51 = wp::iter_next(var_50);
                // column_reference = joint_mimic_joint[column_joint]                         <L 104>
                var_52 = wp::address(var_joint_mimic_joint, var_51);
                var_54 = wp::load(var_52);
                var_53 = wp::copy(var_54);
                // column_multiplier = float(1.0)                                             <L 105>
                var_56 = wp::float(var_55);
                // column_destination_start = joint_qd_start[column_joint]                    <L 106>
                var_57 = wp::address(var_joint_qd_start, var_51);
                var_59 = wp::load(var_57);
                var_58 = wp::copy(var_59);
                // if column_reference >= 0:                                                  <L 107>
                var_61 = (var_53 >= var_60);
                if (var_61) {
                    // column_multiplier = joint_mimic_coeffs[column_joint][1]                <L 108>
                    var_62 = wp::address(var_joint_mimic_coeffs, var_51);
                    var_65 = wp::load(var_62);
                    var_64 = wp::extract(var_65, var_63);
                    // column_destination_start = joint_qd_start[column_reference]            <L 109>
                    var_66 = wp::address(var_joint_qd_start, var_53);
                    var_68 = wp::load(var_66);
                    var_67 = wp::copy(var_68);
                }
                var_69 = wp::where(var_61, var_64, var_56);
                var_70 = wp::where(var_61, var_67, var_58);
                // column_count = joint_qd_start[column_joint + 1] - joint_qd_start[column_joint]       <L 111>
                var_72 = wp::add(var_51, var_71);
                var_73 = wp::address(var_joint_qd_start, var_72);
                var_74 = wp::address(var_joint_qd_start, var_51);
                var_76 = wp::load(var_73);
                var_77 = wp::load(var_74);
                var_75 = wp::sub(var_76, var_77);
                // for column_axis in range(column_count):                                    <L 112>
                var_78 = wp::range(var_75);
                start_for_6:;
                    if (iter_cmp(var_78) == 0) goto end_for_6;
                    var_79 = wp::iter_next(var_78);
                    // source_column = joint_qd_start[column_joint] + column_axis             <L 113>
                    var_80 = wp::address(var_joint_qd_start, var_51);
                    var_82 = wp::load(var_80);
                    var_81 = wp::add(var_82, var_79);
                    // destination_column = column_destination_start + column_axis            <L 114>
                    var_83 = wp::add(var_70, var_79);
                    // value = H[matrix_start + (source_row - dof_start) * matrix_rows + (source_column - dof_start)]       <L 115>
                    var_84 = wp::sub(var_47, var_14);
                    var_85 = wp::mul(var_84, var_11);
                    var_86 = wp::add(var_8, var_85);
                    var_87 = wp::sub(var_81, var_14);
                    var_88 = wp::add(var_86, var_87);
                    var_89 = wp::address(var_H, var_88);
                    var_91 = wp::load(var_89);
                    var_90 = wp::copy(var_91);
                    // if source_row == source_column:                                        <L 116>
                    var_92 = (var_47 == var_81);
                    if (var_92) {
                        // value += joint_armature[source_row]                                <L 117>
                        var_93 = wp::address(var_joint_armature, var_47);
                        var_95 = wp::load(var_93);
                        var_94 = wp::add(var_90, var_95);
                    }
                    var_96 = wp::where(var_92, var_94, var_90);
                    // wp.atomic_add(                                                         <L 118>
                    // H_reduced,                                                             <L 119>
                    // matrix_start + (destination_row - dof_start) * matrix_rows + (destination_column - dof_start),       <L 120>
                    var_97 = wp::sub(var_49, var_14);
                    var_98 = wp::mul(var_97, var_11);
                    var_99 = wp::add(var_8, var_98);
                    var_100 = wp::sub(var_83, var_14);
                    var_101 = wp::add(var_99, var_100);
                    // row_multiplier * column_multiplier * value,                            <L 121>
                    var_102 = wp::mul(var_35, var_69);
                    var_103 = wp::mul(var_102, var_96);
                    var_104 = wp::atomic_add(var_H_reduced, var_101, var_103);
                    goto start_for_6;
                end_for_6:;
                goto start_for_4;
            end_for_4:;
            goto start_for_2;
        end_for_2:;
        goto start_for_0;
    end_for_0:;
    // for joint in range(joint_start, joint_end):                                            <L 127>
    var_105 = wp::range(var_2, var_5);
    start_for_8:;
        if (iter_cmp(var_105) == 0) goto end_for_8;
        var_106 = wp::iter_next(var_105);
        // if joint_mimic_joint[joint] >= 0:                                                  <L 128>
        var_107 = wp::address(var_joint_mimic_joint, var_106);
        var_110 = wp::load(var_107);
        var_109 = (var_110 >= var_108);
        if (var_109) {
            // dof_count = joint_qd_start[joint + 1] - joint_qd_start[joint]                  <L 129>
            var_112 = wp::add(var_106, var_111);
            var_113 = wp::address(var_joint_qd_start, var_112);
            var_114 = wp::address(var_joint_qd_start, var_106);
            var_116 = wp::load(var_113);
            var_117 = wp::load(var_114);
            var_115 = wp::sub(var_116, var_117);
            // for axis in range(dof_count):                                                  <L 130>
            var_118 = wp::range(var_115);
            start_for_10:;
                if (iter_cmp(var_118) == 0) goto end_for_10;
                var_119 = wp::iter_next(var_118);
                // follower_dof = joint_qd_start[joint] + axis - dof_start                    <L 131>
                var_120 = wp::address(var_joint_qd_start, var_106);
                var_122 = wp::load(var_120);
                var_121 = wp::add(var_122, var_119);
                var_123 = wp::sub(var_121, var_14);
                // wp.atomic_add(H_reduced, matrix_start + follower_dof * matrix_rows + follower_dof, 1.0)       <L 132>
                var_124 = wp::mul(var_123, var_11);
                var_125 = wp::add(var_8, var_124);
                var_126 = wp::add(var_125, var_123);
                var_128 = wp::atomic_add(var_H_reduced, var_126, var_127);
                goto start_for_10;
            end_for_10:;
        }
        goto start_for_8;
    end_for_8:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void reduce_mimic_inertia_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_reduce_mimic_inertia *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        reduce_mimic_inertia_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void reduce_mimic_inertia_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_reduce_mimic_inertia *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->articulation_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_end.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_H_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_H_rows.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->articulation_dof_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_mimic_joint.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_mimic_coeffs.strides[0] == (int)sizeof(wp::vec_t<2, wp::float32>)) & (_wp_args->joint_armature.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->H.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->H_reduced.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        reduce_mimic_inertia_cpu_forward_tasks<true>(dim, _wp_args);
    else
        reduce_mimic_inertia_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void reduce_mimic_inertia_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_reduce_mimic_inertia *_wp_args)
{
    reduce_mimic_inertia_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_accumulate_free_distance_joint_f_to_body_force {
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::float32> joint_f_public;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f_ext;
};


template <bool wp_linear_contiguous>
static void accumulate_free_distance_joint_f_to_body_force_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_accumulate_free_distance_joint_f_to_body_force *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_accumulate_free_distance_joint_f_to_body_force wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_type.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_f_public.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.body_f_ext.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::float32> var_joint_f_public = _wp_args->joint_f_public;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_ext = _wp_args->body_f_ext;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    bool var_4{};
    const wp::int32 var_5 = 4;
    bool var_6{};
    const wp::int32 var_7 = 5;
    bool var_8{};
    wp::int32* var_9{};
    wp::int32 var_10{};
    wp::int32 var_11{};
    wp::int32* var_12{};
    wp::int32 var_13{};
    wp::int32 var_14{};
    const wp::int32 var_15 = 0;
    wp::int32 var_16{};
    wp::float32* var_17{};
    const wp::int32 var_18 = 1;
    wp::int32 var_19{};
    wp::float32* var_20{};
    const wp::int32 var_21 = 2;
    wp::int32 var_22{};
    wp::float32* var_23{};
    wp::vec_t<3, wp::float32> var_24{};
    wp::float32 var_25{};
    wp::float32 var_26{};
    wp::float32 var_27{};
    const wp::int32 var_28 = 3;
    wp::int32 var_29{};
    wp::float32* var_30{};
    const wp::int32 var_31 = 4;
    wp::int32 var_32{};
    wp::float32* var_33{};
    const wp::int32 var_34 = 5;
    wp::int32 var_35{};
    wp::float32* var_36{};
    wp::vec_t<3, wp::float32> var_37{};
    wp::float32 var_38{};
    wp::float32 var_39{};
    wp::float32 var_40{};
    wp::vec_t<6, wp::float32> var_41{};
    wp::vec_t<6, wp::float32> var_42{};
    //---------
    // forward
    // def accumulate_free_distance_joint_f_to_body_force(                                    <L 1018>
    // joint_id = wp.tid()                                                                    <L 1026>
    var_0 = builtin_tid1d();
    // jtype = joint_type[joint_id]                                                           <L 1027>
    var_1 = wp::address(var_joint_type, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // if jtype != JointType.FREE and jtype != JointType.DISTANCE:                            <L 1028>
    var_6 = (var_2 != var_5);
    var_4 = var_6;
    if (var_4) {
        var_8 = (var_2 != var_7);
        var_4 = var_4 && var_8;
    }
    if (var_4) {
        // return                                                                             <L 1029>
        return;
    }
    // qd_start = joint_qd_start[joint_id]                                                    <L 1031>
    var_9 = wp::address(var_joint_qd_start, var_0);
    var_11 = wp::load(var_9);
    var_10 = wp::copy(var_11);
    // child = joint_child[joint_id]                                                          <L 1032>
    var_12 = wp::address(var_joint_child, var_0);
    var_14 = wp::load(var_12);
    var_13 = wp::copy(var_14);
    // force = wp.vec3(                                                                       <L 1034>
    // joint_f_public[qd_start + 0],                                                          <L 1035>
    var_16 = wp::add(var_10, var_15);
    var_17 = wp::address(var_joint_f_public, var_16);
    // joint_f_public[qd_start + 1],                                                          <L 1036>
    var_19 = wp::add(var_10, var_18);
    var_20 = wp::address(var_joint_f_public, var_19);
    // joint_f_public[qd_start + 2],                                                          <L 1037>
    var_22 = wp::add(var_10, var_21);
    var_23 = wp::address(var_joint_f_public, var_22);
    var_25 = wp::load(var_17);
    var_26 = wp::load(var_20);
    var_27 = wp::load(var_23);
    var_24 = wp::vec_t<3, wp::float32>(var_25, var_26, var_27);
    // torque_com = wp.vec3(                                                                  <L 1039>
    // joint_f_public[qd_start + 3],                                                          <L 1040>
    var_29 = wp::add(var_10, var_28);
    var_30 = wp::address(var_joint_f_public, var_29);
    // joint_f_public[qd_start + 4],                                                          <L 1041>
    var_32 = wp::add(var_10, var_31);
    var_33 = wp::address(var_joint_f_public, var_32);
    // joint_f_public[qd_start + 5],                                                          <L 1042>
    var_35 = wp::add(var_10, var_34);
    var_36 = wp::address(var_joint_f_public, var_35);
    var_38 = wp::load(var_30);
    var_39 = wp::load(var_33);
    var_40 = wp::load(var_36);
    var_37 = wp::vec_t<3, wp::float32>(var_38, var_39, var_40);
    // wp.atomic_add(body_f_ext, child, wp.spatial_vector(force, torque_com))                 <L 1045>
    var_41 = wp::vec_t<6, wp::float32>(var_24, var_37);
    var_42 = wp::atomic_add(var_body_f_ext, var_13, var_41);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void accumulate_free_distance_joint_f_to_body_force_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_accumulate_free_distance_joint_f_to_body_force *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        accumulate_free_distance_joint_f_to_body_force_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void accumulate_free_distance_joint_f_to_body_force_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_accumulate_free_distance_joint_f_to_body_force *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_type.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_f_public.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->body_f_ext.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)));
    if (wp_linear_contiguous)
        accumulate_free_distance_joint_f_to_body_force_cpu_forward_tasks<true>(dim, _wp_args);
    else
        accumulate_free_distance_joint_f_to_body_force_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void accumulate_free_distance_joint_f_to_body_force_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_accumulate_free_distance_joint_f_to_body_force *_wp_args)
{
    accumulate_free_distance_joint_f_to_body_force_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_dense_cholesky_batched {
    wp::array_t<wp::int32> A_starts;
    wp::array_t<wp::int32> A_dim;
    wp::array_t<wp::int32> R_starts;
    wp::array_t<wp::float32> A;
    wp::array_t<wp::float32> R;
    wp::array_t<wp::float32> L;
};


template <bool wp_linear_contiguous>
static void eval_dense_cholesky_batched_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_dense_cholesky_batched *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_eval_dense_cholesky_batched wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.A_starts.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.A_dim.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.R_starts.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.A.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.R.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.L.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_A_starts = _wp_args->A_starts;
    wp::array_t<wp::int32> var_A_dim = _wp_args->A_dim;
    wp::array_t<wp::int32> var_R_starts = _wp_args->R_starts;
    wp::array_t<wp::float32> var_A = _wp_args->A;
    wp::array_t<wp::float32> var_R = _wp_args->R;
    wp::array_t<wp::float32> var_L = _wp_args->L;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32* var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    //---------
    // forward
    // def eval_dense_cholesky_batched(                                                       <L 1746>
    // batch = wp.tid()                                                                       <L 1754>
    var_0 = builtin_tid1d();
    // n = A_dim[batch]                                                                       <L 1756>
    var_1 = wp::address(var_A_dim, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // A_start = A_starts[batch]                                                              <L 1757>
    var_4 = wp::address(var_A_starts, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // R_start = R_starts[batch]                                                              <L 1758>
    var_7 = wp::address(var_R_starts, var_0);
    var_9 = wp::load(var_7);
    var_8 = wp::copy(var_9);
    // dense_cholesky(n, A, R, A_start, R_start, L)                                           <L 1760>
    dense_cholesky_0(var_2, var_A, var_R, var_5, var_8, var_L);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_dense_cholesky_batched_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_cholesky_batched *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_dense_cholesky_batched_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_dense_cholesky_batched_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_cholesky_batched *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->A_starts.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->A_dim.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->R_starts.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->A.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->R.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->L.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        eval_dense_cholesky_batched_cpu_forward_tasks<true>(dim, _wp_args);
    else
        eval_dense_cholesky_batched_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_dense_cholesky_batched_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_dense_cholesky_batched *_wp_args)
{
    eval_dense_cholesky_batched_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_correct_free_distance_body_pose_from_world_twist {
    wp::array_t<wp::int32> joint_indices;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::transform_t<wp::float32>> body_q_in;
    wp::array_t<wp::vec_t<6, wp::float32>> body_qd_out;
    wp::array_t<wp::transform_t<wp::float32>> body_q_out;
    wp::float32 dt;
};


template <bool wp_linear_contiguous>
static void correct_free_distance_body_pose_from_world_twist_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_correct_free_distance_body_pose_from_world_twist *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_correct_free_distance_body_pose_from_world_twist wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_indices.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.body_com.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.body_q_in.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_qd_out.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        wp_contiguous_args.body_q_out.strides[0] = sizeof(wp::transform_t<wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_indices = _wp_args->joint_indices;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_in = _wp_args->body_q_in;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd_out = _wp_args->body_qd_out;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_out = _wp_args->body_q_out;
    wp::float32 var_dt = _wp_args->dt;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::transform_t<wp::float32>* var_7{};
    wp::vec_t<3, wp::float32>* var_8{};
    wp::vec_t<6, wp::float32>* var_9{};
    wp::transform_t<wp::float32> var_10{};
    wp::transform_t<wp::float32> var_11{};
    wp::vec_t<3, wp::float32> var_12{};
    wp::vec_t<6, wp::float32> var_13{};
    //---------
    // forward
    // def correct_free_distance_body_pose_from_world_twist(                                  <L 1927>
    // joint_id = joint_indices[wp.tid()]                                                     <L 1936>
    var_0 = builtin_tid1d();
    var_1 = wp::address(var_joint_indices, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // child = joint_child[joint_id]                                                          <L 1937>
    var_4 = wp::address(var_joint_child, var_2);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // body_q_out[child] = integrate_body_pose_from_com_twist(body_q_in[child], body_com[child], body_qd_out[child], dt)       <L 1938>
    var_7 = wp::address(var_body_q_in, var_5);
    var_8 = wp::address(var_body_com, var_5);
    var_9 = wp::address(var_body_qd_out, var_5);
    var_11 = wp::load(var_7);
    var_12 = wp::load(var_8);
    var_13 = wp::load(var_9);
    var_10 = integrate_body_pose_from_com_twist_0(var_11, var_12, var_13, var_dt);
    wp::array_store(var_body_q_out, var_5, var_10);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void correct_free_distance_body_pose_from_world_twist_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_correct_free_distance_body_pose_from_world_twist *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        correct_free_distance_body_pose_from_world_twist_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void correct_free_distance_body_pose_from_world_twist_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_correct_free_distance_body_pose_from_world_twist *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_indices.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->body_com.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->body_q_in.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_qd_out.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)) & (_wp_args->body_q_out.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)));
    if (wp_linear_contiguous)
        correct_free_distance_body_pose_from_world_twist_cpu_forward_tasks<true>(dim, _wp_args);
    else
        correct_free_distance_body_pose_from_world_twist_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void correct_free_distance_body_pose_from_world_twist_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_correct_free_distance_body_pose_from_world_twist *_wp_args)
{
    correct_free_distance_body_pose_from_world_twist_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_reconstruct_free_distance_joint_q_from_body_pose {
    wp::array_t<wp::int32> joint_indices;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_c;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::float32> joint_q;
};


template <bool wp_linear_contiguous>
static void reconstruct_free_distance_joint_q_from_body_pose_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_reconstruct_free_distance_joint_q_from_body_pose *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_reconstruct_free_distance_joint_q_from_body_pose wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_indices.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_parent.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_q_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_X_p.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.joint_X_c.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_q.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.joint_q.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_indices = _wp_args->joint_indices;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c = _wp_args->joint_X_c;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::float32> var_joint_q = _wp_args->joint_q;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32* var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::transform_t<wp::float32>* var_10{};
    wp::transform_t<wp::float32> var_11{};
    wp::transform_t<wp::float32> var_12{};
    const wp::int32 var_13 = 0;
    bool var_14{};
    wp::transform_t<wp::float32>* var_15{};
    wp::transform_t<wp::float32> var_16{};
    wp::transform_t<wp::float32> var_17{};
    wp::transform_t<wp::float32> var_18{};
    wp::transform_t<wp::float32>* var_19{};
    wp::transform_t<wp::float32>* var_20{};
    wp::transform_t<wp::float32> var_21{};
    wp::transform_t<wp::float32> var_22{};
    wp::transform_t<wp::float32> var_23{};
    wp::quat_t<wp::float32> var_24{};
    wp::vec_t<3, wp::float32> var_25{};
    wp::vec_t<3, wp::float32> var_26{};
    wp::vec_t<3, wp::float32> var_27{};
    wp::vec_t<3, wp::float32> var_28{};
    wp::quat_t<wp::float32> var_29{};
    wp::quat_t<wp::float32> var_30{};
    wp::quat_t<wp::float32> var_31{};
    wp::quat_t<wp::float32> var_32{};
    wp::int32* var_33{};
    wp::int32 var_34{};
    wp::int32 var_35{};
    const wp::int32 var_36 = 0;
    wp::float32 var_37{};
    const wp::int32 var_38 = 0;
    wp::int32 var_39{};
    const wp::int32 var_40 = 1;
    wp::float32 var_41{};
    const wp::int32 var_42 = 1;
    wp::int32 var_43{};
    const wp::int32 var_44 = 2;
    wp::float32 var_45{};
    const wp::int32 var_46 = 2;
    wp::int32 var_47{};
    const wp::int32 var_48 = 0;
    wp::float32 var_49{};
    const wp::int32 var_50 = 3;
    wp::int32 var_51{};
    const wp::int32 var_52 = 1;
    wp::float32 var_53{};
    const wp::int32 var_54 = 4;
    wp::int32 var_55{};
    const wp::int32 var_56 = 2;
    wp::float32 var_57{};
    const wp::int32 var_58 = 5;
    wp::int32 var_59{};
    const wp::int32 var_60 = 3;
    wp::float32 var_61{};
    const wp::int32 var_62 = 6;
    wp::int32 var_63{};
    //---------
    // forward
    // def reconstruct_free_distance_joint_q_from_body_pose(                                  <L 1103>
    // joint_id = joint_indices[wp.tid()]                                                     <L 1113>
    var_0 = builtin_tid1d();
    var_1 = wp::address(var_joint_indices, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // parent = joint_parent[joint_id]                                                        <L 1114>
    var_4 = wp::address(var_joint_parent, var_2);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // child = joint_child[joint_id]                                                          <L 1115>
    var_7 = wp::address(var_joint_child, var_2);
    var_9 = wp::load(var_7);
    var_8 = wp::copy(var_9);
    // X_wpj = joint_X_p[joint_id]                                                            <L 1117>
    var_10 = wp::address(var_joint_X_p, var_2);
    var_12 = wp::load(var_10);
    var_11 = wp::copy(var_12);
    // if parent >= 0:                                                                        <L 1118>
    var_14 = (var_5 >= var_13);
    if (var_14) {
        // X_wpj = body_q[parent] * X_wpj                                                     <L 1119>
        var_15 = wp::address(var_body_q, var_5);
        var_17 = wp::load(var_15);
        var_16 = wp::mul(var_17, var_11);
    }
    var_18 = wp::where(var_14, var_16, var_11);
    // X_wcj = body_q[child] * joint_X_c[joint_id]                                            <L 1121>
    var_19 = wp::address(var_body_q, var_8);
    var_20 = wp::address(var_joint_X_c, var_2);
    var_22 = wp::load(var_19);
    var_23 = wp::load(var_20);
    var_21 = wp::mul(var_22, var_23);
    // x_err_c = wp.quat_rotate_inv(                                                          <L 1123>
    // wp.transform_get_rotation(X_wpj),                                                      <L 1124>
    var_24 = wp::transform_get_rotation(var_18);
    // wp.transform_get_translation(X_wcj) - wp.transform_get_translation(X_wpj),             <L 1125>
    var_25 = wp::transform_get_translation(var_21);
    var_26 = wp::transform_get_translation(var_18);
    var_27 = wp::sub(var_25, var_26);
    var_28 = wp::quat_rotate_inv(var_24, var_27);
    // q_pc = wp.quat_inverse(wp.transform_get_rotation(X_wpj)) * wp.transform_get_rotation(X_wcj)       <L 1127>
    var_29 = wp::transform_get_rotation(var_18);
    var_30 = wp::quat_inverse(var_29);
    var_31 = wp::transform_get_rotation(var_21);
    var_32 = wp::mul(var_30, var_31);
    // q_start = joint_q_start[joint_id]                                                      <L 1129>
    var_33 = wp::address(var_joint_q_start, var_2);
    var_35 = wp::load(var_33);
    var_34 = wp::copy(var_35);
    // joint_q[q_start + 0] = x_err_c[0]                                                      <L 1130>
    var_37 = wp::extract(var_28, var_36);
    var_39 = wp::add(var_34, var_38);
    wp::array_store(var_joint_q, var_39, var_37);
    // joint_q[q_start + 1] = x_err_c[1]                                                      <L 1131>
    var_41 = wp::extract(var_28, var_40);
    var_43 = wp::add(var_34, var_42);
    wp::array_store(var_joint_q, var_43, var_41);
    // joint_q[q_start + 2] = x_err_c[2]                                                      <L 1132>
    var_45 = wp::extract(var_28, var_44);
    var_47 = wp::add(var_34, var_46);
    wp::array_store(var_joint_q, var_47, var_45);
    // joint_q[q_start + 3] = q_pc[0]                                                         <L 1133>
    var_49 = wp::extract(var_32, var_48);
    var_51 = wp::add(var_34, var_50);
    wp::array_store(var_joint_q, var_51, var_49);
    // joint_q[q_start + 4] = q_pc[1]                                                         <L 1134>
    var_53 = wp::extract(var_32, var_52);
    var_55 = wp::add(var_34, var_54);
    wp::array_store(var_joint_q, var_55, var_53);
    // joint_q[q_start + 5] = q_pc[2]                                                         <L 1135>
    var_57 = wp::extract(var_32, var_56);
    var_59 = wp::add(var_34, var_58);
    wp::array_store(var_joint_q, var_59, var_57);
    // joint_q[q_start + 6] = q_pc[3]                                                         <L 1136>
    var_61 = wp::extract(var_32, var_60);
    var_63 = wp::add(var_34, var_62);
    wp::array_store(var_joint_q, var_63, var_61);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void reconstruct_free_distance_joint_q_from_body_pose_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_reconstruct_free_distance_joint_q_from_body_pose *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        reconstruct_free_distance_joint_q_from_body_pose_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void reconstruct_free_distance_joint_q_from_body_pose_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_reconstruct_free_distance_joint_q_from_body_pose *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_indices.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_parent.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_q_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_X_p.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->joint_X_c.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_q.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->joint_q.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        reconstruct_free_distance_joint_q_from_body_pose_cpu_forward_tasks<true>(dim, _wp_args);
    else
        reconstruct_free_distance_joint_q_from_body_pose_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void reconstruct_free_distance_joint_q_from_body_pose_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_reconstruct_free_distance_joint_q_from_body_pose *_wp_args)
{
    reconstruct_free_distance_joint_q_from_body_pose_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_copy_kinematic_joint_state {
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> body_flags;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::float32> joint_q_in;
    wp::array_t<wp::float32> joint_qd_in;
    wp::array_t<wp::float32> joint_q_out;
    wp::array_t<wp::float32> joint_qd_out;
};


template <bool wp_linear_contiguous>
static void copy_kinematic_joint_state_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_copy_kinematic_joint_state *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_copy_kinematic_joint_state wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_child.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.body_flags.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_q_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_q_in.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.joint_qd_in.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.joint_q_out.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.joint_qd_out.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_body_flags = _wp_args->body_flags;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::float32> var_joint_q_in = _wp_args->joint_q_in;
    wp::array_t<wp::float32> var_joint_qd_in = _wp_args->joint_qd_in;
    wp::array_t<wp::float32> var_joint_q_out = _wp_args->joint_q_out;
    wp::array_t<wp::float32> var_joint_qd_out = _wp_args->joint_qd_out;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    const wp::int32 var_5 = 2;
    wp::int32 var_6{};
    wp::int32 var_7{};
    const wp::int32 var_8 = 0;
    bool var_9{};
    wp::int32* var_10{};
    wp::int32 var_11{};
    wp::int32 var_12{};
    const wp::int32 var_13 = 1;
    wp::int32 var_14{};
    wp::int32* var_15{};
    wp::int32 var_16{};
    wp::int32 var_17{};
    wp::range_t var_18{};
    wp::int32 var_19{};
    wp::float32* var_20{};
    wp::float32 var_21{};
    wp::float32 var_22{};
    wp::int32* var_23{};
    wp::int32 var_24{};
    wp::int32 var_25{};
    const wp::int32 var_26 = 1;
    wp::int32 var_27{};
    wp::int32* var_28{};
    wp::int32 var_29{};
    wp::int32 var_30{};
    wp::range_t var_31{};
    wp::int32 var_32{};
    wp::float32* var_33{};
    wp::float32 var_34{};
    wp::float32 var_35{};
    //---------
    // forward
    // def copy_kinematic_joint_state(                                                        <L 1961>
    // joint_id = wp.tid()                                                                    <L 1972>
    var_0 = builtin_tid1d();
    // child = joint_child[joint_id]                                                          <L 1973>
    var_1 = wp::address(var_joint_child, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // if (body_flags[child] & BodyFlags.KINEMATIC) == 0:                                     <L 1974>
    var_4 = wp::address(var_body_flags, var_2);
    var_7 = wp::load(var_4);
    var_6 = wp::bit_and(var_7, var_5);
    var_9 = (var_6 == var_8);
    if (var_9) {
        // return                                                                             <L 1975>
        return;
    }
    // q_start = joint_q_start[joint_id]                                                      <L 1977>
    var_10 = wp::address(var_joint_q_start, var_0);
    var_12 = wp::load(var_10);
    var_11 = wp::copy(var_12);
    // q_end = joint_q_start[joint_id + 1]                                                    <L 1978>
    var_14 = wp::add(var_0, var_13);
    var_15 = wp::address(var_joint_q_start, var_14);
    var_17 = wp::load(var_15);
    var_16 = wp::copy(var_17);
    // for i in range(q_start, q_end):                                                        <L 1979>
    var_18 = wp::range(var_11, var_16);
    start_for_1:;
        if (iter_cmp(var_18) == 0) goto end_for_1;
        var_19 = wp::iter_next(var_18);
        // joint_q_out[i] = joint_q_in[i]                                                     <L 1980>
        var_20 = wp::address(var_joint_q_in, var_19);
        var_22 = wp::load(var_20);
        var_21 = wp::copy(var_22);
        wp::array_store(var_joint_q_out, var_19, var_21);
        goto start_for_1;
    end_for_1:;
    // qd_start = joint_qd_start[joint_id]                                                    <L 1982>
    var_23 = wp::address(var_joint_qd_start, var_0);
    var_25 = wp::load(var_23);
    var_24 = wp::copy(var_25);
    // qd_end = joint_qd_start[joint_id + 1]                                                  <L 1983>
    var_27 = wp::add(var_0, var_26);
    var_28 = wp::address(var_joint_qd_start, var_27);
    var_30 = wp::load(var_28);
    var_29 = wp::copy(var_30);
    // for i in range(qd_start, qd_end):                                                      <L 1984>
    var_31 = wp::range(var_24, var_29);
    start_for_3:;
        if (iter_cmp(var_31) == 0) goto end_for_3;
        var_32 = wp::iter_next(var_31);
        // joint_qd_out[i] = joint_qd_in[i]                                                   <L 1985>
        var_33 = wp::address(var_joint_qd_in, var_32);
        var_35 = wp::load(var_33);
        var_34 = wp::copy(var_35);
        wp::array_store(var_joint_qd_out, var_32, var_34);
        goto start_for_3;
    end_for_3:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void copy_kinematic_joint_state_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_copy_kinematic_joint_state *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        copy_kinematic_joint_state_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void copy_kinematic_joint_state_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_copy_kinematic_joint_state *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_child.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->body_flags.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_q_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_q_in.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->joint_qd_in.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->joint_q_out.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->joint_qd_out.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        copy_kinematic_joint_state_cpu_forward_tasks<true>(dim, _wp_args);
    else
        copy_kinematic_joint_state_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void copy_kinematic_joint_state_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_copy_kinematic_joint_state *_wp_args)
{
    copy_kinematic_joint_state_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_rigid_tau {
    wp::array_t<bool> articulation_mask;
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_target_q_start;
    wp::array_t<wp::int32> joint_dof_dim;
    wp::array_t<wp::float32> joint_target_q;
    wp::array_t<wp::float32> joint_target_qd;
    wp::array_t<wp::float32> joint_q;
    wp::array_t<wp::float32> joint_qd;
    wp::array_t<wp::float32> joint_f;
    wp::array_t<wp::float32> joint_target_ke;
    wp::array_t<wp::float32> joint_target_kd;
    wp::array_t<wp::float32> joint_limit_lower;
    wp::array_t<wp::float32> joint_limit_upper;
    wp::array_t<wp::float32> joint_limit_ke;
    wp::array_t<wp::float32> joint_limit_kd;
    wp::array_t<wp::float32> joint_damping;
    wp::array_t<wp::vec_t<6, wp::float32>> joint_S_s;
    wp::array_t<wp::transform_t<wp::float32>> body_q_com;
    wp::array_t<wp::vec_t<3, wp::float32>> body_solve_origin;
    wp::array_t<wp::vec_t<6, wp::float32>> body_fb_s;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f_ext;
    wp::array_t<wp::vec_t<6, wp::float32>> body_ft_s;
    wp::array_t<wp::float32> tau;
};


template <bool wp_linear_contiguous>
static void eval_rigid_tau_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_rigid_tau *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;

    //---------
    // argument vars
    wp::array_t<bool> var_articulation_mask = _wp_args->articulation_mask;
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_target_q_start = _wp_args->joint_target_q_start;
    wp::array_t<wp::int32> var_joint_dof_dim = _wp_args->joint_dof_dim;
    wp::array_t<wp::float32> var_joint_target_q = _wp_args->joint_target_q;
    wp::array_t<wp::float32> var_joint_target_qd = _wp_args->joint_target_qd;
    wp::array_t<wp::float32> var_joint_q = _wp_args->joint_q;
    wp::array_t<wp::float32> var_joint_qd = _wp_args->joint_qd;
    wp::array_t<wp::float32> var_joint_f = _wp_args->joint_f;
    wp::array_t<wp::float32> var_joint_target_ke = _wp_args->joint_target_ke;
    wp::array_t<wp::float32> var_joint_target_kd = _wp_args->joint_target_kd;
    wp::array_t<wp::float32> var_joint_limit_lower = _wp_args->joint_limit_lower;
    wp::array_t<wp::float32> var_joint_limit_upper = _wp_args->joint_limit_upper;
    wp::array_t<wp::float32> var_joint_limit_ke = _wp_args->joint_limit_ke;
    wp::array_t<wp::float32> var_joint_limit_kd = _wp_args->joint_limit_kd;
    wp::array_t<wp::float32> var_joint_damping = _wp_args->joint_damping;
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s = _wp_args->joint_S_s;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com = _wp_args->body_q_com;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_solve_origin = _wp_args->body_solve_origin;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_fb_s = _wp_args->body_fb_s;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_ext = _wp_args->body_f_ext;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_ft_s = _wp_args->body_ft_s;
    wp::array_t<wp::float32> var_tau = _wp_args->tau;
    //---------
    // primal vars
    wp::int32 var_0{};
    bool* var_1;
    bool var_2{};
    bool var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32* var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::int32 var_10{};
    wp::range_t var_11{};
    wp::int32 var_12{};
    wp::int32 var_13{};
    const wp::int32 var_14 = 1;
    wp::int32 var_15{};
    wp::int32* var_16{};
    wp::int32 var_17{};
    wp::int32 var_18{};
    wp::int32* var_19{};
    wp::int32 var_20{};
    wp::int32 var_21{};
    wp::int32* var_22{};
    wp::int32 var_23{};
    wp::int32 var_24{};
    wp::int32* var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    wp::int32* var_28{};
    wp::int32 var_29{};
    wp::int32 var_30{};
    wp::int32* var_31{};
    wp::int32 var_32{};
    wp::int32 var_33{};
    const wp::int32 var_34 = 0;
    wp::int32* var_35{};
    wp::int32 var_36{};
    wp::int32 var_37{};
    const wp::int32 var_38 = 1;
    wp::int32* var_39{};
    wp::int32 var_40{};
    wp::int32 var_41{};
    wp::vec_t<6, wp::float32>* var_42{};
    wp::vec_t<6, wp::float32> var_43{};
    wp::vec_t<6, wp::float32> var_44{};
    wp::vec_t<6, wp::float32>* var_45{};
    wp::vec_t<6, wp::float32> var_46{};
    wp::vec_t<6, wp::float32> var_47{};
    wp::vec_t<6, wp::float32>* var_48{};
    wp::vec_t<6, wp::float32> var_49{};
    wp::vec_t<6, wp::float32> var_50{};
    wp::vec_t<3, wp::float32> var_51{};
    wp::vec_t<3, wp::float32> var_52{};
    wp::transform_t<wp::float32>* var_53{};
    wp::vec_t<3, wp::float32> var_54{};
    wp::transform_t<wp::float32> var_55{};
    wp::vec_t<3, wp::float32>* var_56{};
    wp::vec_t<3, wp::float32> var_57{};
    wp::vec_t<3, wp::float32> var_58{};
    wp::vec_t<3, wp::float32> var_59{};
    wp::vec_t<3, wp::float32> var_60{};
    wp::vec_t<6, wp::float32> var_61{};
    wp::vec_t<6, wp::float32> var_62{};
    wp::vec_t<6, wp::float32> var_63{};
    wp::vec_t<6, wp::float32> var_64{};
    const wp::int32 var_65 = 0;
    bool var_66{};
    wp::vec_t<6, wp::float32>* var_67{};
    wp::vec_t<6, wp::float32> var_68{};
    wp::vec_t<6, wp::float32> var_69{};
    //---------
    // forward
    // def eval_rigid_tau(                                                                    <L 1400>
    // index = wp.tid()                                                                       <L 1433>
    var_0 = builtin_tid1d();
    // if articulation_mask:                                                                  <L 1435>
    if (var_articulation_mask) {
        // if not articulation_mask[index]:                                                   <L 1436>
        var_1 = wp::address(var_articulation_mask, var_0);
        var_3 = wp::load(var_1);
        var_2 = wp::unot(var_3);
        if (var_2) {
            // return                                                                         <L 1437>
            return;
        }
    }
    // start = articulation_start[index]                                                      <L 1439>
    var_4 = wp::address(var_articulation_start, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // end = articulation_end[index]                                                          <L 1440>
    var_7 = wp::address(var_articulation_end, var_0);
    var_9 = wp::load(var_7);
    var_8 = wp::copy(var_9);
    // count = end - start                                                                    <L 1441>
    var_10 = wp::sub(var_8, var_5);
    // for offset in range(count):                                                            <L 1444>
    var_11 = wp::range(var_10);
    start_for_1:;
        if (iter_cmp(var_11) == 0) goto end_for_1;
        var_12 = wp::iter_next(var_11);
        // i = end - offset - 1                                                               <L 1446>
        var_13 = wp::sub(var_8, var_12);
        var_15 = wp::sub(var_13, var_14);
        // type = joint_type[i]                                                               <L 1448>
        var_16 = wp::address(var_joint_type, var_15);
        var_18 = wp::load(var_16);
        var_17 = wp::copy(var_18);
        // parent = joint_parent[i]                                                           <L 1449>
        var_19 = wp::address(var_joint_parent, var_15);
        var_21 = wp::load(var_19);
        var_20 = wp::copy(var_21);
        // child = joint_child[i]                                                             <L 1450>
        var_22 = wp::address(var_joint_child, var_15);
        var_24 = wp::load(var_22);
        var_23 = wp::copy(var_24);
        // dof_start = joint_qd_start[i]                                                      <L 1451>
        var_25 = wp::address(var_joint_qd_start, var_15);
        var_27 = wp::load(var_25);
        var_26 = wp::copy(var_27);
        // coord_start = joint_q_start[i]                                                     <L 1452>
        var_28 = wp::address(var_joint_q_start, var_15);
        var_30 = wp::load(var_28);
        var_29 = wp::copy(var_30);
        // target_q_start = joint_target_q_start[i]                                           <L 1453>
        var_31 = wp::address(var_joint_target_q_start, var_15);
        var_33 = wp::load(var_31);
        var_32 = wp::copy(var_33);
        // lin_axis_count = joint_dof_dim[i, 0]                                               <L 1454>
        var_35 = wp::address(var_joint_dof_dim, var_15, var_34);
        var_37 = wp::load(var_35);
        var_36 = wp::copy(var_37);
        // ang_axis_count = joint_dof_dim[i, 1]                                               <L 1455>
        var_39 = wp::address(var_joint_dof_dim, var_15, var_38);
        var_41 = wp::load(var_39);
        var_40 = wp::copy(var_41);
        // f_b_s = body_fb_s[child]                                                           <L 1458>
        var_42 = wp::address(var_body_fb_s, var_23);
        var_44 = wp::load(var_42);
        var_43 = wp::copy(var_44);
        // f_t_s = body_ft_s[child]                                                           <L 1459>
        var_45 = wp::address(var_body_ft_s, var_23);
        var_47 = wp::load(var_45);
        var_46 = wp::copy(var_47);
        // f_ext_public = body_f_ext[child]                                                   <L 1460>
        var_48 = wp::address(var_body_f_ext, var_23);
        var_50 = wp::load(var_48);
        var_49 = wp::copy(var_50);
        // force = wp.spatial_top(f_ext_public)                                               <L 1461>
        var_51 = wp::spatial_top(var_49);
        // torque_com = wp.spatial_bottom(f_ext_public)                                       <L 1462>
        var_52 = wp::spatial_bottom(var_49);
        // x_com_s = wp.transform_get_translation(body_q_com[child]) - body_solve_origin[child]       <L 1463>
        var_53 = wp::address(var_body_q_com, var_23);
        var_55 = wp::load(var_53);
        var_54 = wp::transform_get_translation(var_55);
        var_56 = wp::address(var_body_solve_origin, var_23);
        var_58 = wp::load(var_56);
        var_57 = wp::sub(var_54, var_58);
        // f_ext = -wp.spatial_vector(force, torque_com + wp.cross(x_com_s, force))           <L 1464>
        var_59 = wp::cross(var_57, var_51);
        var_60 = wp::add(var_52, var_59);
        var_61 = wp::vec_t<6, wp::float32>(var_51, var_60);
        var_62 = wp::neg(var_61);
        // body_f_ext[child] = f_ext                                                          <L 1465>
        wp::array_store(var_body_f_ext, var_23, var_62);
        // f_s = f_b_s + f_t_s + f_ext                                                        <L 1466>
        var_63 = wp::add(var_43, var_46);
        var_64 = wp::add(var_63, var_62);
        // jcalc_tau(                                                                         <L 1469>
        // type,                                                                              <L 1470>
        // joint_target_ke,                                                                   <L 1471>
        // joint_target_kd,                                                                   <L 1472>
        // joint_limit_ke,                                                                    <L 1473>
        // joint_limit_kd,                                                                    <L 1474>
        // joint_damping,                                                                     <L 1475>
        // joint_S_s,                                                                         <L 1476>
        // joint_q,                                                                           <L 1477>
        // joint_qd,                                                                          <L 1478>
        // joint_f,                                                                           <L 1479>
        // joint_target_q,                                                                    <L 1480>
        // joint_target_qd,                                                                   <L 1481>
        // joint_limit_lower,                                                                 <L 1482>
        // joint_limit_upper,                                                                 <L 1483>
        // coord_start,                                                                       <L 1484>
        // dof_start,                                                                         <L 1485>
        // target_q_start,                                                                    <L 1486>
        // lin_axis_count,                                                                    <L 1487>
        // ang_axis_count,                                                                    <L 1488>
        // f_s,                                                                               <L 1489>
        // tau,                                                                               <L 1490>
        jcalc_tau_0(var_17, var_joint_target_ke, var_joint_target_kd, var_joint_limit_ke, var_joint_limit_kd, var_joint_damping, var_joint_S_s, var_joint_q, var_joint_qd, var_joint_f, var_joint_target_q, var_joint_target_qd, var_joint_limit_lower, var_joint_limit_upper, var_29, var_26, var_32, var_36, var_40, var_64, var_tau);
        // if parent >= 0:                                                                    <L 1496>
        var_66 = (var_20 >= var_65);
        if (var_66) {
            // body_ft_s[parent] = body_ft_s[parent] + f_s                                    <L 1497>
            var_67 = wp::address(var_body_ft_s, var_20);
            var_69 = wp::load(var_67);
            var_68 = wp::add(var_69, var_64);
            wp::array_store(var_body_ft_s, var_20, var_68);
        }
        goto start_for_1;
    end_for_1:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_rigid_tau_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_tau *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_rigid_tau_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_rigid_tau_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_tau *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    eval_rigid_tau_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_rigid_tau_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_tau *_wp_args)
{
    eval_rigid_tau_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_articulation_fk_with_velocity_conversion_from_joint {
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> articulation_indices;
    wp::array_t<wp::int32> articulation_joint_start;
    wp::array_t<wp::float32> joint_q;
    wp::array_t<wp::float32> joint_qd;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_c;
    wp::array_t<wp::vec_t<3, wp::float32>> joint_axis;
    wp::array_t<wp::int32> joint_dof_dim;
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::vec_t<6, wp::float32>> body_qd;
};


template <bool wp_linear_contiguous>
static void eval_articulation_fk_with_velocity_conversion_from_joint_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_articulation_fk_with_velocity_conversion_from_joint *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;

    //---------
    // argument vars
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_articulation_indices = _wp_args->articulation_indices;
    wp::array_t<wp::int32> var_articulation_joint_start = _wp_args->articulation_joint_start;
    wp::array_t<wp::float32> var_joint_q = _wp_args->joint_q;
    wp::array_t<wp::float32> var_joint_qd = _wp_args->joint_qd;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_c = _wp_args->joint_X_c;
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis = _wp_args->joint_axis;
    wp::array_t<wp::int32> var_joint_dof_dim = _wp_args->joint_dof_dim;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd = _wp_args->body_qd;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32* var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    //---------
    // forward
    // def eval_articulation_fk_with_velocity_conversion_from_joint(                          <L 2230>
    // tid = wp.tid()                                                                         <L 2251>
    var_0 = builtin_tid1d();
    // articulation_id = articulation_indices[tid]                                            <L 2252>
    var_1 = wp::address(var_articulation_indices, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // joint_start = articulation_joint_start[tid]                                            <L 2253>
    var_4 = wp::address(var_articulation_joint_start, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // joint_end = articulation_end[articulation_id]                                          <L 2254>
    var_7 = wp::address(var_articulation_end, var_2);
    var_9 = wp::load(var_7);
    var_8 = wp::copy(var_9);
    // eval_single_articulation_fk_with_velocity_conversion(                                  <L 2256>
    // joint_start,                                                                           <L 2257>
    // joint_end,                                                                             <L 2258>
    // joint_q,                                                                               <L 2259>
    // joint_qd,                                                                              <L 2260>
    // joint_q_start,                                                                         <L 2261>
    // joint_qd_start,                                                                        <L 2262>
    // joint_type,                                                                            <L 2263>
    // joint_parent,                                                                          <L 2264>
    // joint_child,                                                                           <L 2265>
    // joint_X_p,                                                                             <L 2266>
    // joint_X_c,                                                                             <L 2267>
    // joint_axis,                                                                            <L 2268>
    // joint_dof_dim,                                                                         <L 2269>
    // body_com,                                                                              <L 2270>
    // body_q,                                                                                <L 2272>
    // body_qd,                                                                               <L 2273>
    eval_single_articulation_fk_with_velocity_conversion_0(var_5, var_8, var_joint_q, var_joint_qd, var_joint_q_start, var_joint_qd_start, var_joint_type, var_joint_parent, var_joint_child, var_joint_X_p, var_joint_X_c, var_joint_axis, var_joint_dof_dim, var_body_com, var_body_q, var_body_qd);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_articulation_fk_with_velocity_conversion_from_joint_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_articulation_fk_with_velocity_conversion_from_joint *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_articulation_fk_with_velocity_conversion_from_joint_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_articulation_fk_with_velocity_conversion_from_joint_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_articulation_fk_with_velocity_conversion_from_joint *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    eval_articulation_fk_with_velocity_conversion_from_joint_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_articulation_fk_with_velocity_conversion_from_joint_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_articulation_fk_with_velocity_conversion_from_joint *_wp_args)
{
    eval_articulation_fk_with_velocity_conversion_from_joint_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_expand_mimic_accelerations {
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::int32> joint_mimic_joint;
    wp::array_t<wp::vec_t<2, wp::float32>> joint_mimic_coeffs;
    wp::array_t<wp::float32> joint_qdd;
};


template <bool wp_linear_contiguous>
static void expand_mimic_accelerations_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_expand_mimic_accelerations *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_expand_mimic_accelerations wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.joint_qd_start.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_mimic_joint.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.joint_mimic_coeffs.strides[0] = sizeof(wp::vec_t<2, wp::float32>);
        wp_contiguous_args.joint_qdd.strides[0] = sizeof(wp::float32);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::int32> var_joint_mimic_joint = _wp_args->joint_mimic_joint;
    wp::array_t<wp::vec_t<2, wp::float32>> var_joint_mimic_coeffs = _wp_args->joint_mimic_coeffs;
    wp::array_t<wp::float32> var_joint_qdd = _wp_args->joint_qdd;
    //---------
    // primal vars
    wp::int32 var_0{};
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    const wp::int32 var_4 = 0;
    bool var_5{};
    wp::vec_t<2, wp::float32>* var_6{};
    const wp::int32 var_7 = 1;
    wp::float32 var_8{};
    wp::vec_t<2, wp::float32> var_9{};
    const wp::int32 var_10 = 1;
    wp::int32 var_11{};
    wp::int32* var_12{};
    wp::int32* var_13{};
    wp::int32 var_14{};
    wp::int32 var_15{};
    wp::int32 var_16{};
    wp::range_t var_17{};
    wp::int32 var_18{};
    wp::int32* var_19{};
    wp::int32 var_20{};
    wp::int32 var_21{};
    wp::float32* var_22{};
    wp::float32 var_23{};
    wp::float32 var_24{};
    wp::int32* var_25{};
    wp::int32 var_26{};
    wp::int32 var_27{};
    //---------
    // forward
    // def expand_mimic_accelerations(                                                        <L 168>
    // joint = wp.tid()                                                                       <L 175>
    var_0 = builtin_tid1d();
    // reference = joint_mimic_joint[joint]                                                   <L 176>
    var_1 = wp::address(var_joint_mimic_joint, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // if reference < 0:                                                                      <L 177>
    var_5 = (var_2 < var_4);
    if (var_5) {
        // return                                                                             <L 178>
        return;
    }
    // multiplier = joint_mimic_coeffs[joint][1]                                              <L 180>
    var_6 = wp::address(var_joint_mimic_coeffs, var_0);
    var_9 = wp::load(var_6);
    var_8 = wp::extract(var_9, var_7);
    // dof_count = joint_qd_start[joint + 1] - joint_qd_start[joint]                          <L 181>
    var_11 = wp::add(var_0, var_10);
    var_12 = wp::address(var_joint_qd_start, var_11);
    var_13 = wp::address(var_joint_qd_start, var_0);
    var_15 = wp::load(var_12);
    var_16 = wp::load(var_13);
    var_14 = wp::sub(var_15, var_16);
    // for axis in range(dof_count):                                                          <L 182>
    var_17 = wp::range(var_14);
    start_for_1:;
        if (iter_cmp(var_17) == 0) goto end_for_1;
        var_18 = wp::iter_next(var_17);
        // joint_qdd[joint_qd_start[joint] + axis] = multiplier * joint_qdd[joint_qd_start[reference] + axis]       <L 183>
        var_19 = wp::address(var_joint_qd_start, var_2);
        var_21 = wp::load(var_19);
        var_20 = wp::add(var_21, var_18);
        var_22 = wp::address(var_joint_qdd, var_20);
        var_24 = wp::load(var_22);
        var_23 = wp::mul(var_8, var_24);
        var_25 = wp::address(var_joint_qd_start, var_0);
        var_27 = wp::load(var_25);
        var_26 = wp::add(var_27, var_18);
        wp::array_store(var_joint_qdd, var_26, var_23);
        goto start_for_1;
    end_for_1:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void expand_mimic_accelerations_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_expand_mimic_accelerations *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        expand_mimic_accelerations_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void expand_mimic_accelerations_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_expand_mimic_accelerations *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->joint_qd_start.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_mimic_joint.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->joint_mimic_coeffs.strides[0] == (int)sizeof(wp::vec_t<2, wp::float32>)) & (_wp_args->joint_qdd.strides[0] == (int)sizeof(wp::float32)));
    if (wp_linear_contiguous)
        expand_mimic_accelerations_cpu_forward_tasks<true>(dim, _wp_args);
    else
        expand_mimic_accelerations_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void expand_mimic_accelerations_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_expand_mimic_accelerations *_wp_args)
{
    expand_mimic_accelerations_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C

struct wp_args_eval_rigid_id {
    wp::array_t<bool> articulation_mask;
    wp::array_t<wp::int32> articulation_start;
    wp::array_t<wp::int32> articulation_end;
    wp::array_t<wp::int32> joint_type;
    wp::array_t<wp::int32> joint_parent;
    wp::array_t<wp::int32> joint_child;
    wp::array_t<wp::int32> joint_q_start;
    wp::array_t<wp::int32> joint_qd_start;
    wp::array_t<wp::float32> joint_q;
    wp::array_t<wp::float32> joint_qd;
    wp::array_t<wp::float32> joint_qdd;
    wp::array_t<wp::vec_t<3, wp::float32>> joint_axis;
    wp::array_t<wp::int32> joint_dof_dim;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> body_I_m;
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::transform_t<wp::float32>> body_q_com;
    wp::array_t<wp::transform_t<wp::float32>> joint_X_p;
    wp::array_t<wp::int32> body_world;
    wp::array_t<wp::vec_t<3, wp::float32>> gravity;
    wp::array_t<wp::vec_t<6, wp::float32>> body_qd;
    wp::array_t<wp::vec_t<6, wp::float32>> joint_S_s;
    wp::array_t<wp::vec_t<3, wp::float32>> body_solve_origin;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> body_I_s;
    wp::array_t<wp::vec_t<6, wp::float32>> body_v_s;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f_s;
    wp::array_t<wp::vec_t<6, wp::float32>> body_a_s;
};


template <bool wp_linear_contiguous>
static void eval_rigid_id_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_rigid_id *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;

    //---------
    // argument vars
    wp::array_t<bool> var_articulation_mask = _wp_args->articulation_mask;
    wp::array_t<wp::int32> var_articulation_start = _wp_args->articulation_start;
    wp::array_t<wp::int32> var_articulation_end = _wp_args->articulation_end;
    wp::array_t<wp::int32> var_joint_type = _wp_args->joint_type;
    wp::array_t<wp::int32> var_joint_parent = _wp_args->joint_parent;
    wp::array_t<wp::int32> var_joint_child = _wp_args->joint_child;
    wp::array_t<wp::int32> var_joint_q_start = _wp_args->joint_q_start;
    wp::array_t<wp::int32> var_joint_qd_start = _wp_args->joint_qd_start;
    wp::array_t<wp::float32> var_joint_q = _wp_args->joint_q;
    wp::array_t<wp::float32> var_joint_qd = _wp_args->joint_qd;
    wp::array_t<wp::float32> var_joint_qdd = _wp_args->joint_qdd;
    wp::array_t<wp::vec_t<3, wp::float32>> var_joint_axis = _wp_args->joint_axis;
    wp::array_t<wp::int32> var_joint_dof_dim = _wp_args->joint_dof_dim;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_m = _wp_args->body_I_m;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::transform_t<wp::float32>> var_body_q_com = _wp_args->body_q_com;
    wp::array_t<wp::transform_t<wp::float32>> var_joint_X_p = _wp_args->joint_X_p;
    wp::array_t<wp::int32> var_body_world = _wp_args->body_world;
    wp::array_t<wp::vec_t<3, wp::float32>> var_gravity = _wp_args->gravity;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd = _wp_args->body_qd;
    wp::array_t<wp::vec_t<6, wp::float32>> var_joint_S_s = _wp_args->joint_S_s;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_solve_origin = _wp_args->body_solve_origin;
    wp::array_t<wp::mat_t<6, 6, wp::float32>> var_body_I_s = _wp_args->body_I_s;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_v_s = _wp_args->body_v_s;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f_s = _wp_args->body_f_s;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_a_s = _wp_args->body_a_s;
    //---------
    // primal vars
    wp::int32 var_0{};
    bool* var_1;
    bool var_2{};
    bool var_3{};
    wp::int32* var_4{};
    wp::int32 var_5{};
    wp::int32 var_6{};
    wp::int32* var_7{};
    wp::int32 var_8{};
    wp::int32 var_9{};
    wp::vec_t<3, wp::float32> var_10{};
    bool var_11{};
    wp::int32 var_12{};
    wp::int32* var_13{};
    wp::int32 var_14{};
    wp::int32 var_15{};
    bool var_16{};
    const wp::int32 var_17 = 4;
    bool var_18{};
    const wp::int32 var_19 = 5;
    bool var_20{};
    wp::int32* var_21{};
    wp::int32 var_22{};
    wp::transform_t<wp::float32>* var_23{};
    wp::vec_t<3, wp::float32> var_24{};
    wp::transform_t<wp::float32> var_25{};
    wp::vec_t<3, wp::float32> var_26{};
    wp::vec_t<3, wp::float32> var_27{};
    wp::range_t var_28{};
    wp::int32 var_29{};
    //---------
    // forward
    // def eval_rigid_id(                                                                     <L 1319>
    // index = wp.tid()                                                                       <L 1349>
    var_0 = builtin_tid1d();
    // if articulation_mask:                                                                  <L 1351>
    if (var_articulation_mask) {
        // if not articulation_mask[index]:                                                   <L 1352>
        var_1 = wp::address(var_articulation_mask, var_0);
        var_3 = wp::load(var_1);
        var_2 = wp::unot(var_3);
        if (var_2) {
            // return                                                                         <L 1353>
            return;
        }
    }
    // start = articulation_start[index]                                                      <L 1355>
    var_4 = wp::address(var_articulation_start, var_0);
    var_6 = wp::load(var_4);
    var_5 = wp::copy(var_6);
    // end = articulation_end[index]                                                          <L 1356>
    var_7 = wp::address(var_articulation_end, var_0);
    var_9 = wp::load(var_7);
    var_8 = wp::copy(var_9);
    // solve_origin = wp.vec3()                                                               <L 1358>
    var_10 = wp::vec_t<3, wp::float32>();
    // if start < end:                                                                        <L 1359>
    var_11 = (var_5 < var_8);
    if (var_11) {
        // root = start                                                                       <L 1360>
        var_12 = wp::copy(var_5);
        // root_type = joint_type[root]                                                       <L 1361>
        var_13 = wp::address(var_joint_type, var_12);
        var_15 = wp::load(var_13);
        var_14 = wp::copy(var_15);
        // if root_type == JointType.FREE or root_type == JointType.DISTANCE:                 <L 1362>
        var_18 = (var_14 == var_17);
        var_16 = var_18;
        if (!var_16) {
            var_20 = (var_14 == var_19);
            var_16 = var_16 || var_20;
        }
        if (var_16) {
            // solve_origin = wp.transform_get_translation(body_q_com[joint_child[root]])       <L 1366>
            var_21 = wp::address(var_joint_child, var_12);
            var_22 = wp::load(var_21);
            var_23 = wp::address(var_body_q_com, var_22);
            var_25 = wp::load(var_23);
            var_24 = wp::transform_get_translation(var_25);
        }
        var_26 = wp::where(var_16, var_24, var_10);
    }
    var_27 = wp::where(var_11, var_26, var_10);
    // for i in range(start, end):                                                            <L 1369>
    var_28 = wp::range(var_5, var_8);
    start_for_1:;
        if (iter_cmp(var_28) == 0) goto end_for_1;
        var_29 = wp::iter_next(var_28);
        // compute_link_velocity(                                                             <L 1370>
        // i,                                                                                 <L 1371>
        // solve_origin,                                                                      <L 1372>
        // joint_type,                                                                        <L 1373>
        // joint_parent,                                                                      <L 1374>
        // joint_child,                                                                       <L 1375>
        // joint_q_start,                                                                     <L 1376>
        // joint_qd_start,                                                                    <L 1377>
        // joint_q,                                                                           <L 1378>
        // joint_qd,                                                                          <L 1379>
        // joint_qdd,                                                                         <L 1380>
        // joint_axis,                                                                        <L 1381>
        // joint_dof_dim,                                                                     <L 1382>
        // body_I_m,                                                                          <L 1383>
        // body_q,                                                                            <L 1384>
        // body_q_com,                                                                        <L 1385>
        // joint_X_p,                                                                         <L 1386>
        // body_world,                                                                        <L 1387>
        // gravity,                                                                           <L 1388>
        // body_qd,                                                                           <L 1389>
        // joint_S_s,                                                                         <L 1390>
        // body_solve_origin,                                                                 <L 1391>
        // body_I_s,                                                                          <L 1392>
        // body_v_s,                                                                          <L 1393>
        // body_f_s,                                                                          <L 1394>
        // body_a_s,                                                                          <L 1395>
        compute_link_velocity_0(var_29, var_27, var_joint_type, var_joint_parent, var_joint_child, var_joint_q_start, var_joint_qd_start, var_joint_q, var_joint_qd, var_joint_qdd, var_joint_axis, var_joint_dof_dim, var_body_I_m, var_body_q, var_body_q_com, var_joint_X_p, var_body_world, var_gravity, var_body_qd, var_joint_S_s, var_body_solve_origin, var_body_I_s, var_body_v_s, var_body_f_s, var_body_a_s);
        goto start_for_1;
    end_for_1:;
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_rigid_id_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_id *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_rigid_id_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_rigid_id_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_id *_wp_args)
{
    wp::tile_shared_storage_t tile_mem;
#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)
    wp::shared_tile_storage = &tile_mem;
#endif

    eval_rigid_id_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_rigid_id_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_rigid_id *_wp_args)
{
    eval_rigid_id_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C


#pragma clang diagnostic pop
