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


// warp/_src/math.py:0
static wp::float32 norm_huber_0(
    wp::vec_t<3, wp::float32> var_v,
    wp::float32 var_delta)
{
    //---------
    // primal vars
    wp::float32 var_0{};
    wp::float32 var_1{};
    bool var_2{};
    const wp::float32 var_3 = 0.5;
    wp::float32 var_4{};
    wp::float32 var_5{};
    const wp::float32 var_6 = 0.5;
    wp::float32 var_7{};
    wp::float32 var_8{};
    wp::float32 var_9{};
    //---------
    // forward
    // def norm_huber(v: Any, delta: float = 1.0) -> float:                                   <L 1>
    // a = wp.dot(v, v)                                                                       <L 17>
    var_0 = wp::dot(var_v, var_v);
    // if a <= delta * delta:                                                                 <L 18>
    var_1 = wp::mul(var_delta, var_delta);
    var_2 = (var_0 <= var_1);
    if (var_2) {
        // return 0.5 * a                                                                     <L 19>
        var_4 = wp::mul(var_3, var_0);
        return var_4;
    }
    // return delta * (wp.sqrt(a) - 0.5 * delta)                                              <L 20>
    var_5 = wp::sqrt(var_0);
    var_7 = wp::mul(var_6, var_delta);
    var_8 = wp::sub(var_5, var_7);
    var_9 = wp::mul(var_delta, var_8);
    return var_9;
}


// newton/_src/solvers/semi_implicit/kernels_contact.py:381
static void _eval_body_contact_0(
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd,
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com,
    wp::array_t<wp::float32> var_shape_material_ke,
    wp::array_t<wp::float32> var_shape_material_kd,
    wp::array_t<wp::float32> var_shape_material_kf,
    wp::array_t<wp::float32> var_shape_material_ka,
    wp::array_t<wp::float32> var_shape_material_mu,
    wp::array_t<wp::int32> var_shape_body,
    wp::array_t<wp::int32> var_contact_count,
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_point0,
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_point1,
    wp::vec_t<3, wp::float32> var_contact_surface_velocity,
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_normal,
    wp::array_t<wp::int32> var_contact_shape0,
    wp::array_t<wp::int32> var_contact_shape1,
    wp::array_t<wp::float32> var_contact_margin0,
    wp::array_t<wp::float32> var_contact_margin1,
    wp::array_t<wp::float32> var_rigid_contact_stiffness,
    wp::array_t<wp::float32> var_rigid_contact_damping,
    wp::array_t<wp::float32> var_rigid_contact_friction_scale,
    bool var_force_in_world_frame,
    wp::float32 var_friction_smoothing,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f,
    wp::int32 var_tid)
{
    //---------
    // primal vars
    const wp::int32 var_0 = 0;
    wp::int32* var_1{};
    wp::int32 var_2{};
    wp::int32 var_3{};
    bool var_4{};
    const wp::float32 var_5 = 0.0;
    const wp::float32 var_6 = 0.0;
    const wp::float32 var_7 = 0.0;
    const wp::float32 var_8 = 0.0;
    const wp::float32 var_9 = 0.0;
    const wp::int32 var_10 = 0;
    wp::float32* var_11{};
    wp::float32 var_12{};
    wp::float32 var_13{};
    wp::float32* var_14{};
    wp::float32 var_15{};
    wp::float32 var_16{};
    wp::int32* var_17{};
    wp::int32 var_18{};
    wp::int32 var_19{};
    wp::int32* var_20{};
    wp::int32 var_21{};
    wp::int32 var_22{};
    bool var_23{};
    const wp::int32 var_24 = -1;
    const wp::int32 var_25 = -1;
    const wp::int32 var_26 = 0;
    bool var_27{};
    const wp::int32 var_28 = 1;
    wp::int32 var_29{};
    wp::float32* var_30{};
    wp::float32 var_31{};
    wp::float32 var_32{};
    wp::float32* var_33{};
    wp::float32 var_34{};
    wp::float32 var_35{};
    wp::float32* var_36{};
    wp::float32 var_37{};
    wp::float32 var_38{};
    wp::float32* var_39{};
    wp::float32 var_40{};
    wp::float32 var_41{};
    wp::float32* var_42{};
    wp::float32 var_43{};
    wp::float32 var_44{};
    wp::int32* var_45{};
    wp::int32 var_46{};
    wp::int32 var_47{};
    wp::float32 var_48{};
    wp::float32 var_49{};
    wp::float32 var_50{};
    wp::float32 var_51{};
    wp::float32 var_52{};
    wp::int32 var_53{};
    wp::int32 var_54{};
    const wp::int32 var_55 = 0;
    bool var_56{};
    const wp::int32 var_57 = 1;
    wp::int32 var_58{};
    wp::float32* var_59{};
    wp::float32 var_60{};
    wp::float32 var_61{};
    wp::float32* var_62{};
    wp::float32 var_63{};
    wp::float32 var_64{};
    wp::float32* var_65{};
    wp::float32 var_66{};
    wp::float32 var_67{};
    wp::float32* var_68{};
    wp::float32 var_69{};
    wp::float32 var_70{};
    wp::float32* var_71{};
    wp::float32 var_72{};
    wp::float32 var_73{};
    wp::int32* var_74{};
    wp::int32 var_75{};
    wp::int32 var_76{};
    wp::float32 var_77{};
    wp::float32 var_78{};
    wp::float32 var_79{};
    wp::float32 var_80{};
    wp::float32 var_81{};
    wp::int32 var_82{};
    wp::int32 var_83{};
    const wp::int32 var_84 = 0;
    bool var_85{};
    wp::float32 var_86{};
    wp::float32 var_87{};
    wp::float32 var_88{};
    wp::float32 var_89{};
    wp::float32 var_90{};
    wp::float32 var_91{};
    wp::float32 var_92{};
    wp::float32 var_93{};
    wp::float32 var_94{};
    wp::float32 var_95{};
    wp::float32 var_96{};
    wp::float32 var_97{};
    wp::float32 var_98{};
    wp::float32 var_99{};
    wp::float32 var_100{};
    wp::float32* var_101{};
    wp::float32 var_102{};
    wp::float32 var_103{};
    const wp::float32 var_104 = 0.0;
    bool var_105{};
    wp::float32 var_106{};
    wp::float32* var_107{};
    wp::float32 var_108{};
    wp::float32 var_109{};
    const wp::float32 var_110 = 0.0;
    bool var_111{};
    wp::float32 var_112{};
    wp::float32* var_113{};
    wp::float32 var_114{};
    wp::float32 var_115{};
    const wp::float32 var_116 = 0.0;
    bool var_117{};
    wp::float32 var_118{};
    wp::float32 var_119{};
    wp::float32 var_120{};
    wp::float32 var_121{};
    wp::float32 var_122{};
    wp::vec_t<3, wp::float32>* var_123{};
    wp::vec_t<3, wp::float32> var_124{};
    wp::vec_t<3, wp::float32> var_125{};
    wp::vec_t<3, wp::float32>* var_126{};
    wp::vec_t<3, wp::float32> var_127{};
    wp::vec_t<3, wp::float32> var_128{};
    wp::vec_t<3, wp::float32>* var_129{};
    wp::vec_t<3, wp::float32> var_130{};
    wp::vec_t<3, wp::float32> var_131{};
    const wp::float32 var_132 = 0.0;
    wp::vec_t<3, wp::float32> var_133{};
    const wp::float32 var_134 = 0.0;
    wp::vec_t<3, wp::float32> var_135{};
    const wp::int32 var_136 = 0;
    bool var_137{};
    wp::transform_t<wp::float32>* var_138{};
    wp::transform_t<wp::float32> var_139{};
    wp::transform_t<wp::float32> var_140{};
    wp::vec_t<3, wp::float32>* var_141{};
    wp::vec_t<3, wp::float32> var_142{};
    wp::vec_t<3, wp::float32> var_143{};
    wp::vec_t<3, wp::float32> var_144{};
    wp::vec_t<3, wp::float32> var_145{};
    wp::vec_t<3, wp::float32> var_146{};
    wp::vec_t<3, wp::float32> var_147{};
    wp::vec_t<3, wp::float32> var_148{};
    wp::vec_t<3, wp::float32> var_149{};
    wp::vec_t<3, wp::float32> var_150{};
    const wp::int32 var_151 = 0;
    bool var_152{};
    wp::transform_t<wp::float32>* var_153{};
    wp::transform_t<wp::float32> var_154{};
    wp::transform_t<wp::float32> var_155{};
    wp::vec_t<3, wp::float32>* var_156{};
    wp::vec_t<3, wp::float32> var_157{};
    wp::vec_t<3, wp::float32> var_158{};
    wp::vec_t<3, wp::float32> var_159{};
    wp::vec_t<3, wp::float32> var_160{};
    wp::vec_t<3, wp::float32> var_161{};
    wp::vec_t<3, wp::float32> var_162{};
    wp::vec_t<3, wp::float32> var_163{};
    wp::vec_t<3, wp::float32> var_164{};
    wp::vec_t<3, wp::float32> var_165{};
    wp::vec_t<3, wp::float32> var_166{};
    wp::float32 var_167{};
    bool var_168{};
    const wp::float32 var_169 = 0.0;
    wp::vec_t<3, wp::float32> var_170{};
    const wp::float32 var_171 = 0.0;
    wp::vec_t<3, wp::float32> var_172{};
    const wp::int32 var_173 = 0;
    bool var_174{};
    wp::vec_t<6, wp::float32>* var_175{};
    wp::vec_t<6, wp::float32> var_176{};
    wp::vec_t<6, wp::float32> var_177{};
    wp::vec_t<3, wp::float32> var_178{};
    wp::vec_t<3, wp::float32> var_179{};
    wp::vec_t<3, wp::float32> var_180{};
    wp::vec_t<3, wp::float32> var_181{};
    wp::vec_t<3, wp::float32> var_182{};
    wp::vec_t<3, wp::float32> var_183{};
    wp::vec_t<3, wp::float32> var_184{};
    wp::vec_t<3, wp::float32> var_185{};
    const wp::int32 var_186 = 0;
    bool var_187{};
    wp::vec_t<6, wp::float32>* var_188{};
    wp::vec_t<6, wp::float32> var_189{};
    wp::vec_t<6, wp::float32> var_190{};
    wp::vec_t<3, wp::float32> var_191{};
    wp::vec_t<3, wp::float32> var_192{};
    wp::vec_t<3, wp::float32> var_193{};
    wp::vec_t<3, wp::float32> var_194{};
    wp::vec_t<3, wp::float32> var_195{};
    wp::vec_t<3, wp::float32> var_196{};
    wp::vec_t<3, wp::float32> var_197{};
    wp::vec_t<3, wp::float32> var_198{};
    wp::vec_t<3, wp::float32> var_199{};
    wp::float32 var_200{};
    wp::vec_t<3, wp::float32> var_201{};
    wp::vec_t<3, wp::float32> var_202{};
    wp::vec_t<3, wp::float32> var_203{};
    wp::float32 var_204{};
    wp::vec_t<3, wp::float32> var_205{};
    wp::vec_t<3, wp::float32> var_206{};
    wp::float32 var_207{};
    const wp::float32 var_208 = 0.0;
    wp::float32 var_209{};
    wp::float32 var_210{};
    wp::float32 var_211{};
    wp::float32 var_212{};
    const wp::float32 var_213 = 0.0;
    wp::vec_t<3, wp::float32> var_214{};
    const wp::float32 var_215 = 0.0;
    bool var_216{};
    wp::float32 var_217{};
    const wp::float32 var_218 = 0.0;
    bool var_219{};
    wp::vec_t<3, wp::float32> var_220{};
    wp::float32 var_221{};
    wp::float32 var_222{};
    wp::float32 var_223{};
    wp::float32 var_224{};
    wp::float32 var_225{};
    wp::vec_t<3, wp::float32> var_226{};
    wp::vec_t<3, wp::float32> var_227{};
    wp::vec_t<3, wp::float32> var_228{};
    wp::float32 var_229{};
    wp::vec_t<3, wp::float32> var_230{};
    wp::vec_t<3, wp::float32> var_231{};
    const wp::int32 var_232 = 0;
    bool var_233{};
    wp::vec_t<3, wp::float32> var_234{};
    wp::vec_t<6, wp::float32> var_235{};
    wp::vec_t<6, wp::float32> var_236{};
    wp::vec_t<3, wp::float32> var_237{};
    wp::vec_t<6, wp::float32> var_238{};
    wp::vec_t<6, wp::float32> var_239{};
    const wp::int32 var_240 = 0;
    bool var_241{};
    wp::vec_t<3, wp::float32> var_242{};
    wp::vec_t<6, wp::float32> var_243{};
    wp::vec_t<6, wp::float32> var_244{};
    wp::vec_t<3, wp::float32> var_245{};
    wp::vec_t<6, wp::float32> var_246{};
    wp::vec_t<6, wp::float32> var_247{};
    //---------
    // forward
    // def _eval_body_contact(                                                                <L 382>
    // count = contact_count[0]                                                               <L 410>
    var_1 = wp::address(var_contact_count, var_0);
    var_3 = wp::load(var_1);
    var_2 = wp::copy(var_3);
    // if tid >= count:                                                                       <L 411>
    var_4 = (var_tid >= var_2);
    if (var_4) {
        // return                                                                             <L 412>
        return;
    }
    // ke = 0.0  # contact normal force stiffness                                             <L 415>
    // kd = 0.0  # damping coefficient                                                        <L 416>
    // kf = 0.0  # contact friction gain                                                      <L 417>
    // ka = 0.0  # adhesion distance                                                          <L 418>
    // mu = 0.0  # friction coefficient                                                       <L 419>
    // mat_nonzero = 0                                                                        <L 420>
    // margin_a = contact_margin0[tid]                                                        <L 421>
    var_11 = wp::address(var_contact_margin0, var_tid);
    var_13 = wp::load(var_11);
    var_12 = wp::copy(var_13);
    // margin_b = contact_margin1[tid]                                                        <L 422>
    var_14 = wp::address(var_contact_margin1, var_tid);
    var_16 = wp::load(var_14);
    var_15 = wp::copy(var_16);
    // shape_a = contact_shape0[tid]                                                          <L 423>
    var_17 = wp::address(var_contact_shape0, var_tid);
    var_19 = wp::load(var_17);
    var_18 = wp::copy(var_19);
    // shape_b = contact_shape1[tid]                                                          <L 424>
    var_20 = wp::address(var_contact_shape1, var_tid);
    var_22 = wp::load(var_20);
    var_21 = wp::copy(var_22);
    // if shape_a == shape_b:                                                                 <L 425>
    var_23 = (var_18 == var_21);
    if (var_23) {
        // return                                                                             <L 426>
        return;
    }
    // body_a = -1                                                                            <L 427>
    // body_b = -1                                                                            <L 428>
    // if shape_a >= 0:                                                                       <L 429>
    var_27 = (var_18 >= var_26);
    if (var_27) {
        // mat_nonzero += 1                                                                   <L 430>
        var_29 = wp::add(var_10, var_28);
        // ke += shape_material_ke[shape_a]                                                   <L 431>
        var_30 = wp::address(var_shape_material_ke, var_18);
        var_32 = wp::load(var_30);
        var_31 = wp::add(var_5, var_32);
        // kd += shape_material_kd[shape_a]                                                   <L 432>
        var_33 = wp::address(var_shape_material_kd, var_18);
        var_35 = wp::load(var_33);
        var_34 = wp::add(var_6, var_35);
        // kf += shape_material_kf[shape_a]                                                   <L 433>
        var_36 = wp::address(var_shape_material_kf, var_18);
        var_38 = wp::load(var_36);
        var_37 = wp::add(var_7, var_38);
        // ka += shape_material_ka[shape_a]                                                   <L 434>
        var_39 = wp::address(var_shape_material_ka, var_18);
        var_41 = wp::load(var_39);
        var_40 = wp::add(var_8, var_41);
        // mu += shape_material_mu[shape_a]                                                   <L 435>
        var_42 = wp::address(var_shape_material_mu, var_18);
        var_44 = wp::load(var_42);
        var_43 = wp::add(var_9, var_44);
        // body_a = shape_body[shape_a]                                                       <L 436>
        var_45 = wp::address(var_shape_body, var_18);
        var_47 = wp::load(var_45);
        var_46 = wp::copy(var_47);
    }
    var_48 = wp::where(var_27, var_31, var_5);
    var_49 = wp::where(var_27, var_34, var_6);
    var_50 = wp::where(var_27, var_37, var_7);
    var_51 = wp::where(var_27, var_40, var_8);
    var_52 = wp::where(var_27, var_43, var_9);
    var_53 = wp::where(var_27, var_29, var_10);
    var_54 = wp::where(var_27, var_46, var_24);
    // if shape_b >= 0:                                                                       <L 437>
    var_56 = (var_21 >= var_55);
    if (var_56) {
        // mat_nonzero += 1                                                                   <L 438>
        var_58 = wp::add(var_53, var_57);
        // ke += shape_material_ke[shape_b]                                                   <L 439>
        var_59 = wp::address(var_shape_material_ke, var_21);
        var_61 = wp::load(var_59);
        var_60 = wp::add(var_48, var_61);
        // kd += shape_material_kd[shape_b]                                                   <L 440>
        var_62 = wp::address(var_shape_material_kd, var_21);
        var_64 = wp::load(var_62);
        var_63 = wp::add(var_49, var_64);
        // kf += shape_material_kf[shape_b]                                                   <L 441>
        var_65 = wp::address(var_shape_material_kf, var_21);
        var_67 = wp::load(var_65);
        var_66 = wp::add(var_50, var_67);
        // ka += shape_material_ka[shape_b]                                                   <L 442>
        var_68 = wp::address(var_shape_material_ka, var_21);
        var_70 = wp::load(var_68);
        var_69 = wp::add(var_51, var_70);
        // mu += shape_material_mu[shape_b]                                                   <L 443>
        var_71 = wp::address(var_shape_material_mu, var_21);
        var_73 = wp::load(var_71);
        var_72 = wp::add(var_52, var_73);
        // body_b = shape_body[shape_b]                                                       <L 444>
        var_74 = wp::address(var_shape_body, var_21);
        var_76 = wp::load(var_74);
        var_75 = wp::copy(var_76);
    }
    var_77 = wp::where(var_56, var_60, var_48);
    var_78 = wp::where(var_56, var_63, var_49);
    var_79 = wp::where(var_56, var_66, var_50);
    var_80 = wp::where(var_56, var_69, var_51);
    var_81 = wp::where(var_56, var_72, var_52);
    var_82 = wp::where(var_56, var_58, var_53);
    var_83 = wp::where(var_56, var_75, var_25);
    // if mat_nonzero > 0:                                                                    <L 445>
    var_85 = (var_82 > var_84);
    if (var_85) {
        // ke /= float(mat_nonzero)                                                           <L 446>
        var_86 = wp::float(var_82);
        var_87 = wp::div(var_77, var_86);
        // kd /= float(mat_nonzero)                                                           <L 447>
        var_88 = wp::float(var_82);
        var_89 = wp::div(var_78, var_88);
        // kf /= float(mat_nonzero)                                                           <L 448>
        var_90 = wp::float(var_82);
        var_91 = wp::div(var_79, var_90);
        // ka /= float(mat_nonzero)                                                           <L 449>
        var_92 = wp::float(var_82);
        var_93 = wp::div(var_80, var_92);
        // mu /= float(mat_nonzero)                                                           <L 450>
        var_94 = wp::float(var_82);
        var_95 = wp::div(var_81, var_94);
    }
    var_96 = wp::where(var_85, var_87, var_77);
    var_97 = wp::where(var_85, var_89, var_78);
    var_98 = wp::where(var_85, var_91, var_79);
    var_99 = wp::where(var_85, var_93, var_80);
    var_100 = wp::where(var_85, var_95, var_81);
    // if rigid_contact_stiffness:                                                            <L 453>
    if (var_rigid_contact_stiffness) {
        // contact_ke = rigid_contact_stiffness[tid]                                          <L 454>
        var_101 = wp::address(var_rigid_contact_stiffness, var_tid);
        var_103 = wp::load(var_101);
        var_102 = wp::copy(var_103);
        // ke = contact_ke if contact_ke > 0.0 else ke                                        <L 455>
        var_105 = (var_102 > var_104);
        if (var_105) {
        }
        if (!var_105) {
        }
        var_106 = wp::where(var_105, var_102, var_96);
        // contact_kd = rigid_contact_damping[tid]                                            <L 456>
        var_107 = wp::address(var_rigid_contact_damping, var_tid);
        var_109 = wp::load(var_107);
        var_108 = wp::copy(var_109);
        // kd = contact_kd if contact_kd > 0.0 else kd                                        <L 457>
        var_111 = (var_108 > var_110);
        if (var_111) {
        }
        if (!var_111) {
        }
        var_112 = wp::where(var_111, var_108, var_97);
        // contact_mu = rigid_contact_friction_scale[tid]                                     <L 458>
        var_113 = wp::address(var_rigid_contact_friction_scale, var_tid);
        var_115 = wp::load(var_113);
        var_114 = wp::copy(var_115);
        // mu = mu * contact_mu if contact_mu > 0.0 else mu                                   <L 459>
        var_117 = (var_114 > var_116);
        if (var_117) {
            var_118 = wp::mul(var_100, var_114);
        }
        if (!var_117) {
        }
        var_119 = wp::where(var_117, var_118, var_100);
    }
    var_120 = wp::where(var_rigid_contact_stiffness, var_106, var_96);
    var_121 = wp::where(var_rigid_contact_stiffness, var_112, var_97);
    var_122 = wp::where(var_rigid_contact_stiffness, var_119, var_100);
    // n = -contact_normal[tid]                                                               <L 463>
    var_123 = wp::address(var_contact_normal, var_tid);
    var_125 = wp::load(var_123);
    var_124 = wp::neg(var_125);
    // bx_a = contact_point0[tid]                                                             <L 464>
    var_126 = wp::address(var_contact_point0, var_tid);
    var_128 = wp::load(var_126);
    var_127 = wp::copy(var_128);
    // bx_b = contact_point1[tid]                                                             <L 465>
    var_129 = wp::address(var_contact_point1, var_tid);
    var_131 = wp::load(var_129);
    var_130 = wp::copy(var_131);
    // r_a = wp.vec3(0.0)                                                                     <L 466>
    var_133 = wp::vec_t<3, wp::float32>(var_132);
    // r_b = wp.vec3(0.0)                                                                     <L 467>
    var_135 = wp::vec_t<3, wp::float32>(var_134);
    // if body_a >= 0:                                                                        <L 468>
    var_137 = (var_54 >= var_136);
    if (var_137) {
        // X_wb_a = body_q[body_a]                                                            <L 469>
        var_138 = wp::address(var_body_q, var_54);
        var_140 = wp::load(var_138);
        var_139 = wp::copy(var_140);
        // X_com_a = body_com[body_a]                                                         <L 470>
        var_141 = wp::address(var_body_com, var_54);
        var_143 = wp::load(var_141);
        var_142 = wp::copy(var_143);
        // bx_a = wp.transform_point(X_wb_a, bx_a) - margin_a * n                             <L 471>
        var_144 = wp::transform_point(var_139, var_127);
        var_145 = wp::mul(var_12, var_124);
        var_146 = wp::sub(var_144, var_145);
        // r_a = bx_a - wp.transform_point(X_wb_a, X_com_a)                                   <L 472>
        var_147 = wp::transform_point(var_139, var_142);
        var_148 = wp::sub(var_146, var_147);
    }
    var_149 = wp::where(var_137, var_146, var_127);
    var_150 = wp::where(var_137, var_148, var_133);
    // if body_b >= 0:                                                                        <L 474>
    var_152 = (var_83 >= var_151);
    if (var_152) {
        // X_wb_b = body_q[body_b]                                                            <L 475>
        var_153 = wp::address(var_body_q, var_83);
        var_155 = wp::load(var_153);
        var_154 = wp::copy(var_155);
        // X_com_b = body_com[body_b]                                                         <L 476>
        var_156 = wp::address(var_body_com, var_83);
        var_158 = wp::load(var_156);
        var_157 = wp::copy(var_158);
        // bx_b = wp.transform_point(X_wb_b, bx_b) + margin_b * n                             <L 477>
        var_159 = wp::transform_point(var_154, var_130);
        var_160 = wp::mul(var_15, var_124);
        var_161 = wp::add(var_159, var_160);
        // r_b = bx_b - wp.transform_point(X_wb_b, X_com_b)                                   <L 478>
        var_162 = wp::transform_point(var_154, var_157);
        var_163 = wp::sub(var_161, var_162);
    }
    var_164 = wp::where(var_152, var_161, var_130);
    var_165 = wp::where(var_152, var_163, var_135);
    // d = wp.dot(n, bx_a - bx_b)                                                             <L 480>
    var_166 = wp::sub(var_149, var_164);
    var_167 = wp::dot(var_124, var_166);
    // if d >= ka:                                                                            <L 482>
    var_168 = (var_167 >= var_99);
    if (var_168) {
        // return                                                                             <L 483>
        return;
    }
    // bv_a = wp.vec3(0.0)                                                                    <L 486>
    var_170 = wp::vec_t<3, wp::float32>(var_169);
    // bv_b = wp.vec3(0.0)                                                                    <L 487>
    var_172 = wp::vec_t<3, wp::float32>(var_171);
    // if body_a >= 0:                                                                        <L 488>
    var_174 = (var_54 >= var_173);
    if (var_174) {
        // body_v_s_a = body_qd[body_a]                                                       <L 489>
        var_175 = wp::address(var_body_qd, var_54);
        var_177 = wp::load(var_175);
        var_176 = wp::copy(var_177);
        // body_w_a = wp.spatial_bottom(body_v_s_a)                                           <L 490>
        var_178 = wp::spatial_bottom(var_176);
        // body_v_a = wp.spatial_top(body_v_s_a)                                              <L 491>
        var_179 = wp::spatial_top(var_176);
        // if force_in_world_frame:                                                           <L 492>
        if (var_force_in_world_frame) {
            // bv_a = body_v_a + wp.cross(body_w_a, bx_a)                                     <L 493>
            var_180 = wp::cross(var_178, var_149);
            var_181 = wp::add(var_179, var_180);
        }
        if (!var_force_in_world_frame) {
            // bv_a = body_v_a + wp.cross(body_w_a, r_a)                                      <L 495>
            var_182 = wp::cross(var_178, var_150);
            var_183 = wp::add(var_179, var_182);
        }
        var_184 = wp::where(var_force_in_world_frame, var_181, var_183);
    }
    var_185 = wp::where(var_174, var_184, var_170);
    // if body_b >= 0:                                                                        <L 497>
    var_187 = (var_83 >= var_186);
    if (var_187) {
        // body_v_s_b = body_qd[body_b]                                                       <L 498>
        var_188 = wp::address(var_body_qd, var_83);
        var_190 = wp::load(var_188);
        var_189 = wp::copy(var_190);
        // body_w_b = wp.spatial_bottom(body_v_s_b)                                           <L 499>
        var_191 = wp::spatial_bottom(var_189);
        // body_v_b = wp.spatial_top(body_v_s_b)                                              <L 500>
        var_192 = wp::spatial_top(var_189);
        // if force_in_world_frame:                                                           <L 501>
        if (var_force_in_world_frame) {
            // bv_b = body_v_b + wp.cross(body_w_b, bx_b)                                     <L 502>
            var_193 = wp::cross(var_191, var_164);
            var_194 = wp::add(var_192, var_193);
        }
        if (!var_force_in_world_frame) {
            // bv_b = body_v_b + wp.cross(body_w_b, r_b)                                      <L 504>
            var_195 = wp::cross(var_191, var_165);
            var_196 = wp::add(var_192, var_195);
        }
        var_197 = wp::where(var_force_in_world_frame, var_194, var_196);
    }
    var_198 = wp::where(var_187, var_197, var_172);
    // v = bv_a - bv_b                                                                        <L 507>
    var_199 = wp::sub(var_185, var_198);
    // surface_velocity_t = contact_surface_velocity - n * wp.dot(n, contact_surface_velocity)       <L 508>
    var_200 = wp::dot(var_124, var_contact_surface_velocity);
    var_201 = wp::mul(var_124, var_200);
    var_202 = wp::sub(var_contact_surface_velocity, var_201);
    // v -= surface_velocity_t                                                                <L 509>
    var_203 = wp::sub(var_199, var_202);
    // vn = wp.dot(n, v)                                                                      <L 514>
    var_204 = wp::dot(var_124, var_203);
    // vt = v - n * vn                                                                        <L 515>
    var_205 = wp::mul(var_124, var_204);
    var_206 = wp::sub(var_203, var_205);
    // fn = d * ke                                                                            <L 518>
    var_207 = wp::mul(var_167, var_120);
    // fd = wp.min(vn, 0.0) * kd * wp.step(d)                                                 <L 521>
    var_209 = wp::min(var_204, var_208);
    var_210 = wp::mul(var_209, var_121);
    var_211 = wp::step(var_167);
    var_212 = wp::mul(var_210, var_211);
    // ft = wp.vec3(0.0)                                                                      <L 536>
    var_214 = wp::vec_t<3, wp::float32>(var_213);
    // if d < 0.0:                                                                            <L 537>
    var_216 = (var_167 < var_215);
    if (var_216) {
        // vs = wp.norm_huber(vt, delta=friction_smoothing)                                   <L 539>
        var_217 = norm_huber_0(var_206, var_friction_smoothing);
        // if vs > 0.0:                                                                       <L 540>
        var_219 = (var_217 > var_218);
        if (var_219) {
            // fr = vt / vs                                                                   <L 541>
            var_220 = wp::div(var_206, var_217);
            // ft = fr * wp.min(kf * vs, -mu * (fn + fd))                                     <L 542>
            var_221 = wp::mul(var_98, var_217);
            var_222 = wp::neg(var_122);
            var_223 = wp::add(var_207, var_212);
            var_224 = wp::mul(var_222, var_223);
            var_225 = wp::min(var_221, var_224);
            var_226 = wp::mul(var_220, var_225);
        }
        var_227 = wp::where(var_219, var_226, var_214);
    }
    var_228 = wp::where(var_216, var_227, var_214);
    // f_total = n * (fn + fd) + ft                                                           <L 544>
    var_229 = wp::add(var_207, var_212);
    var_230 = wp::mul(var_124, var_229);
    var_231 = wp::add(var_230, var_228);
    // if body_a >= 0:                                                                        <L 548>
    var_233 = (var_54 >= var_232);
    if (var_233) {
        // if force_in_world_frame:                                                           <L 549>
        if (var_force_in_world_frame) {
            // wp.atomic_add(body_f, body_a, wp.spatial_vector(f_total, wp.cross(bx_a, f_total)))       <L 550>
            var_234 = wp::cross(var_149, var_231);
            var_235 = wp::vec_t<6, wp::float32>(var_231, var_234);
            var_236 = wp::atomic_add(var_body_f, var_54, var_235);
        }
        if (!var_force_in_world_frame) {
            // wp.atomic_sub(body_f, body_a, wp.spatial_vector(f_total, wp.cross(r_a, f_total)))       <L 552>
            var_237 = wp::cross(var_150, var_231);
            var_238 = wp::vec_t<6, wp::float32>(var_231, var_237);
            var_239 = wp::atomic_sub(var_body_f, var_54, var_238);
        }
    }
    // if body_b >= 0:                                                                        <L 554>
    var_241 = (var_83 >= var_240);
    if (var_241) {
        // if force_in_world_frame:                                                           <L 555>
        if (var_force_in_world_frame) {
            // wp.atomic_sub(body_f, body_b, wp.spatial_vector(f_total, wp.cross(bx_b, f_total)))       <L 556>
            var_242 = wp::cross(var_164, var_231);
            var_243 = wp::vec_t<6, wp::float32>(var_231, var_242);
            var_244 = wp::atomic_sub(var_body_f, var_83, var_243);
        }
        if (!var_force_in_world_frame) {
            // wp.atomic_add(body_f, body_b, wp.spatial_vector(f_total, wp.cross(r_b, f_total)))       <L 558>
            var_245 = wp::cross(var_165, var_231);
            var_246 = wp::vec_t<6, wp::float32>(var_231, var_245);
            var_247 = wp::atomic_add(var_body_f, var_83, var_246);
        }
    }
}


// warp/_src/math.py:0
static void adj_norm_huber_0(
    wp::vec_t<3, wp::float32> var_v,
    wp::float32 var_delta,
    wp::vec_t<3, wp::float32> & adj_v,
    wp::float32 & adj_delta,
    wp::float32 & adj_ret)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}


// newton/_src/solvers/semi_implicit/kernels_contact.py:381
static void adj__eval_body_contact_0(
    wp::array_t<wp::transform_t<wp::float32>> var_body_q,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd,
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com,
    wp::array_t<wp::float32> var_shape_material_ke,
    wp::array_t<wp::float32> var_shape_material_kd,
    wp::array_t<wp::float32> var_shape_material_kf,
    wp::array_t<wp::float32> var_shape_material_ka,
    wp::array_t<wp::float32> var_shape_material_mu,
    wp::array_t<wp::int32> var_shape_body,
    wp::array_t<wp::int32> var_contact_count,
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_point0,
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_point1,
    wp::vec_t<3, wp::float32> var_contact_surface_velocity,
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_normal,
    wp::array_t<wp::int32> var_contact_shape0,
    wp::array_t<wp::int32> var_contact_shape1,
    wp::array_t<wp::float32> var_contact_margin0,
    wp::array_t<wp::float32> var_contact_margin1,
    wp::array_t<wp::float32> var_rigid_contact_stiffness,
    wp::array_t<wp::float32> var_rigid_contact_damping,
    wp::array_t<wp::float32> var_rigid_contact_friction_scale,
    bool var_force_in_world_frame,
    wp::float32 var_friction_smoothing,
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f,
    wp::int32 var_tid,
    wp::array_t<wp::transform_t<wp::float32>> & adj_body_q,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_body_qd,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_body_com,
    wp::array_t<wp::float32> & adj_shape_material_ke,
    wp::array_t<wp::float32> & adj_shape_material_kd,
    wp::array_t<wp::float32> & adj_shape_material_kf,
    wp::array_t<wp::float32> & adj_shape_material_ka,
    wp::array_t<wp::float32> & adj_shape_material_mu,
    wp::array_t<wp::int32> & adj_shape_body,
    wp::array_t<wp::int32> & adj_contact_count,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_contact_point0,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_contact_point1,
    wp::vec_t<3, wp::float32> & adj_contact_surface_velocity,
    wp::array_t<wp::vec_t<3, wp::float32>> & adj_contact_normal,
    wp::array_t<wp::int32> & adj_contact_shape0,
    wp::array_t<wp::int32> & adj_contact_shape1,
    wp::array_t<wp::float32> & adj_contact_margin0,
    wp::array_t<wp::float32> & adj_contact_margin1,
    wp::array_t<wp::float32> & adj_rigid_contact_stiffness,
    wp::array_t<wp::float32> & adj_rigid_contact_damping,
    wp::array_t<wp::float32> & adj_rigid_contact_friction_scale,
    bool & adj_force_in_world_frame,
    wp::float32 & adj_friction_smoothing,
    wp::array_t<wp::vec_t<6, wp::float32>> & adj_body_f,
    wp::int32 & adj_tid)
{
	// reverse mode disabled (no backward-enabled kernel depends on this function)
}

struct wp_args_eval_body_contact {
    wp::array_t<wp::transform_t<wp::float32>> body_q;
    wp::array_t<wp::vec_t<6, wp::float32>> body_qd;
    wp::array_t<wp::vec_t<3, wp::float32>> body_com;
    wp::array_t<wp::float32> shape_material_ke;
    wp::array_t<wp::float32> shape_material_kd;
    wp::array_t<wp::float32> shape_material_kf;
    wp::array_t<wp::float32> shape_material_ka;
    wp::array_t<wp::float32> shape_material_mu;
    wp::array_t<wp::int32> shape_body;
    wp::array_t<wp::int32> contact_count;
    wp::array_t<wp::vec_t<3, wp::float32>> contact_point0;
    wp::array_t<wp::vec_t<3, wp::float32>> contact_point1;
    wp::array_t<wp::vec_t<3, wp::float32>> contact_surface_velocity;
    wp::array_t<wp::vec_t<3, wp::float32>> contact_normal;
    wp::array_t<wp::int32> contact_shape0;
    wp::array_t<wp::int32> contact_shape1;
    wp::array_t<wp::float32> contact_margin0;
    wp::array_t<wp::float32> contact_margin1;
    wp::array_t<wp::float32> rigid_contact_stiffness;
    wp::array_t<wp::float32> rigid_contact_damping;
    wp::array_t<wp::float32> rigid_contact_friction_scale;
    bool force_in_world_frame;
    wp::float32 friction_smoothing;
    wp::array_t<wp::vec_t<6, wp::float32>> body_f;
};


template <bool wp_linear_contiguous>
static void eval_body_contact_cpu_kernel_forward(
    wp::launch_bounds_t<1> dim,
    size_t task_index,
    wp_args_eval_body_contact *_wp_args)
{
    static constexpr bool wp_linear_tid = wp_linear_contiguous;
    wp_args_eval_body_contact wp_contiguous_args;
    if constexpr (wp_linear_contiguous)
    {
        wp_contiguous_args = *_wp_args;
        wp_contiguous_args.body_q.strides[0] = sizeof(wp::transform_t<wp::float32>);
        wp_contiguous_args.body_qd.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        wp_contiguous_args.body_com.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.shape_material_ke.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.shape_material_kd.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.shape_material_kf.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.shape_material_ka.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.shape_material_mu.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.shape_body.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.contact_count.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.contact_point0.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.contact_point1.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.contact_surface_velocity.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.contact_normal.strides[0] = sizeof(wp::vec_t<3, wp::float32>);
        wp_contiguous_args.contact_shape0.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.contact_shape1.strides[0] = sizeof(wp::int32);
        wp_contiguous_args.contact_margin0.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.contact_margin1.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.rigid_contact_stiffness.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.rigid_contact_damping.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.rigid_contact_friction_scale.strides[0] = sizeof(wp::float32);
        wp_contiguous_args.body_f.strides[0] = sizeof(wp::vec_t<6, wp::float32>);
        _wp_args = &wp_contiguous_args;
    }
    //---------
    // argument vars
    wp::array_t<wp::transform_t<wp::float32>> var_body_q = _wp_args->body_q;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_qd = _wp_args->body_qd;
    wp::array_t<wp::vec_t<3, wp::float32>> var_body_com = _wp_args->body_com;
    wp::array_t<wp::float32> var_shape_material_ke = _wp_args->shape_material_ke;
    wp::array_t<wp::float32> var_shape_material_kd = _wp_args->shape_material_kd;
    wp::array_t<wp::float32> var_shape_material_kf = _wp_args->shape_material_kf;
    wp::array_t<wp::float32> var_shape_material_ka = _wp_args->shape_material_ka;
    wp::array_t<wp::float32> var_shape_material_mu = _wp_args->shape_material_mu;
    wp::array_t<wp::int32> var_shape_body = _wp_args->shape_body;
    wp::array_t<wp::int32> var_contact_count = _wp_args->contact_count;
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_point0 = _wp_args->contact_point0;
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_point1 = _wp_args->contact_point1;
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_surface_velocity = _wp_args->contact_surface_velocity;
    wp::array_t<wp::vec_t<3, wp::float32>> var_contact_normal = _wp_args->contact_normal;
    wp::array_t<wp::int32> var_contact_shape0 = _wp_args->contact_shape0;
    wp::array_t<wp::int32> var_contact_shape1 = _wp_args->contact_shape1;
    wp::array_t<wp::float32> var_contact_margin0 = _wp_args->contact_margin0;
    wp::array_t<wp::float32> var_contact_margin1 = _wp_args->contact_margin1;
    wp::array_t<wp::float32> var_rigid_contact_stiffness = _wp_args->rigid_contact_stiffness;
    wp::array_t<wp::float32> var_rigid_contact_damping = _wp_args->rigid_contact_damping;
    wp::array_t<wp::float32> var_rigid_contact_friction_scale = _wp_args->rigid_contact_friction_scale;
    bool var_force_in_world_frame = _wp_args->force_in_world_frame;
    wp::float32 var_friction_smoothing = _wp_args->friction_smoothing;
    wp::array_t<wp::vec_t<6, wp::float32>> var_body_f = _wp_args->body_f;
    //---------
    // primal vars
    wp::int32 var_0{};
    const wp::float32 var_1 = 0.0;
    wp::vec_t<3, wp::float32> var_2{};
    wp::vec_t<3, wp::float32>* var_3{};
    wp::vec_t<3, wp::float32> var_4{};
    wp::vec_t<3, wp::float32> var_5{};
    wp::vec_t<3, wp::float32> var_6{};
    //---------
    // forward
    // def eval_body_contact(                                                                 <L 562>
    // tid = wp.tid()                                                                         <L 588>
    var_0 = builtin_tid1d();
    // surface_velocity = wp.vec3(0.0)                                                        <L 589>
    var_2 = wp::vec_t<3, wp::float32>(var_1);
    // if contact_surface_velocity:                                                           <L 590>
    if (var_contact_surface_velocity) {
        // surface_velocity = contact_surface_velocity[tid]                                   <L 591>
        var_3 = wp::address(var_contact_surface_velocity, var_0);
        var_5 = wp::load(var_3);
        var_4 = wp::copy(var_5);
    }
    var_6 = wp::where(var_contact_surface_velocity, var_4, var_2);
    // _eval_body_contact(                                                                    <L 592>
    // body_q,                                                                                <L 593>
    // body_qd,                                                                               <L 594>
    // body_com,                                                                              <L 595>
    // shape_material_ke,                                                                     <L 596>
    // shape_material_kd,                                                                     <L 597>
    // shape_material_kf,                                                                     <L 598>
    // shape_material_ka,                                                                     <L 599>
    // shape_material_mu,                                                                     <L 600>
    // shape_body,                                                                            <L 601>
    // contact_count,                                                                         <L 602>
    // contact_point0,                                                                        <L 603>
    // contact_point1,                                                                        <L 604>
    // surface_velocity,                                                                      <L 605>
    // contact_normal,                                                                        <L 606>
    // contact_shape0,                                                                        <L 607>
    // contact_shape1,                                                                        <L 608>
    // contact_margin0,                                                                       <L 609>
    // contact_margin1,                                                                       <L 610>
    // rigid_contact_stiffness,                                                               <L 611>
    // rigid_contact_damping,                                                                 <L 612>
    // rigid_contact_friction_scale,                                                          <L 613>
    // force_in_world_frame,                                                                  <L 614>
    // friction_smoothing,                                                                    <L 615>
    // body_f,                                                                                <L 616>
    // tid,                                                                                   <L 617>
    _eval_body_contact_0(var_body_q, var_body_qd, var_body_com, var_shape_material_ke, var_shape_material_kd, var_shape_material_kf, var_shape_material_ka, var_shape_material_mu, var_shape_body, var_contact_count, var_contact_point0, var_contact_point1, var_6, var_contact_normal, var_contact_shape0, var_contact_shape1, var_contact_margin0, var_contact_margin1, var_rigid_contact_stiffness, var_rigid_contact_damping, var_rigid_contact_friction_scale, var_force_in_world_frame, var_friction_smoothing, var_body_f, var_0);
}



template <bool wp_linear_contiguous>
static WP_FORCEINLINE void eval_body_contact_cpu_forward_tasks(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_body_contact *_wp_args)
{
    for (size_t task_index = 0; task_index < dim->size; ++task_index)
    {
        eval_body_contact_cpu_kernel_forward<wp_linear_contiguous>(*dim, task_index, _wp_args);
    }
}

static WP_FORCEINLINE void eval_body_contact_cpu_forward_dispatch(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_body_contact *_wp_args)
{

    const bool wp_linear_contiguous = dim->coord_mult == 1 && ((_wp_args->body_q.strides[0] == (int)sizeof(wp::transform_t<wp::float32>)) & (_wp_args->body_qd.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)) & (_wp_args->body_com.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->shape_material_ke.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->shape_material_kd.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->shape_material_kf.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->shape_material_ka.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->shape_material_mu.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->shape_body.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->contact_count.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->contact_point0.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->contact_point1.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->contact_surface_velocity.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->contact_normal.strides[0] == (int)sizeof(wp::vec_t<3, wp::float32>)) & (_wp_args->contact_shape0.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->contact_shape1.strides[0] == (int)sizeof(wp::int32)) & (_wp_args->contact_margin0.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->contact_margin1.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->rigid_contact_stiffness.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->rigid_contact_damping.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->rigid_contact_friction_scale.strides[0] == (int)sizeof(wp::float32)) & (_wp_args->body_f.strides[0] == (int)sizeof(wp::vec_t<6, wp::float32>)));
    if (wp_linear_contiguous)
        eval_body_contact_cpu_forward_tasks<true>(dim, _wp_args);
    else
        eval_body_contact_cpu_forward_tasks<false>(dim, _wp_args);
}

extern "C" {

// Python CPU entry points
static void eval_body_contact_cpu_forward(
    wp::launch_bounds_t<1> *dim,
    wp_args_eval_body_contact *_wp_args)
{
    eval_body_contact_cpu_forward_dispatch(dim, _wp_args);
}

} // extern C


#pragma clang diagnostic pop
