// ============================================================
// W3 周六实验：NEON 点积（4 个版本）
//
// 平台：ARMv7 NEON / ARMv8-A 的 AArch32 模式
// 运行：Compiler Explorer 选 "ARM64 GCC" 或 "ARM32 GCC"，勾选 Execute
//
// 目标：
//   1. 写出 4 个版本，结果和标量版本一致
//   2. 对比它们的汇编，理解 vmla / 多累加器 / 归约
// ============================================================
#include <arm_neon.h>

#include <math.h>
#include <stdio.h>

// ------------------------------------------------------------
// 版本 1：标量参考实现（已给出）
// ------------------------------------------------------------
float dot_scalar(const float* a, const float* b, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; ++i) {
        s += a[i] * b[i];
    }
    return s;
}

// ------------------------------------------------------------
// 版本 2：标量 + 4 路展开
// 目标：用 4 个独立累加器打破依赖链
// ------------------------------------------------------------
float dot_unrolled(const float* a, const float* b, int n)
{
    // 1: 定义 4 个累加器 s0 s1 s2 s3，初始化为 0
    float s0 = 0;
    float s1 = 0;
    float s2 = 0;
    float s3 = 0;
    // 2: 主循环每次 i += 4，把 4 个乘积分别累加到 s0..s3
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 = a[i+0]*b[i+0]+s0;
        s1 = a[i+1]*b[i+1]+s1;
        s2 = a[i+2]*b[i+2]+s2;
        s3 = a[i+3]*b[i+3]+s3;
    }
    // 3: 把 4 个累加器合并成 sum
    float sum = s0+s1+s2+s3;

    // 4: 处理剩余元素（n 未必是 4 的倍数）
    for(;i<n;i++){
        sum = a[i]*b[i]+sum;
    }
    return sum;
}

// ------------------------------------------------------------
// 版本 3：NEON（单累加器）
// 目标：vld1q 读 4 个 float，vfmaq 累加，最后 vaddvq 归约
// ------------------------------------------------------------
float dot_neon(const float* a, const float* b, int n)
{
    //  1: 定义累加器
    //         float32x4_t acc = vdupq_n_f32(0.0f);   // 4 个 lane 全置 0
    float32x4_t acc = vdupq_n_f32(0.0f);
    //  2: 主循环，每轮处理 4 个 float
    //         acc = vfmaq_f32(acc, vld1q_f32(a + i), vld1q_f32(b + i));
    int i = 0;
    for(;i+4<=n;i+=4){
        float32x4_t q0 = vld1q_f32(a+i);
        float32x4_t q1 = vld1q_f32(b+i);
        acc = vfmaq_f32(acc,q0,q1);
    }
    // 3: ARM64 横向归约（一条指令；AArch32 才需要写两次 vpadd_f32）
    //         float sum = vaddvq_f32(acc);
    float sum = vaddvq_f32(acc);
    // 4: 处理剩下不满 4 个的元素（标量累加）
    for(;i<n;i++){
        sum = a[i]*b[i]+sum;
    }
    return sum;
}

// ------------------------------------------------------------
// 版本 4：NEON + 4 个累加器
// 目标：4 个独立的 acc，每次处理 16 个 float，最后合并
// 为什么要这么做？因为 vfmaq acc, ... 依赖上一次的 acc，
// 形成一条串行依赖链；4 个 acc 能让流水线并行起来。
// ------------------------------------------------------------
float dot_neon4(const float* a, const float* b, int n)
{
    // 1: 定义 4 个累加器，全部 vdupq_n_f32(0.0f)
    //         acc0 / acc1 / acc2 / acc3
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    float32x4_t acc2 = vdupq_n_f32(0.0f);
    float32x4_t acc3 = vdupq_n_f32(0.0f);
    float32x4_t acc = vdupq_n_f32(0.0f);
    // 2: 主循环每次 i += 16，四条 vfmaq_f32 分别累加到 acc0..acc3
    //         每条的偏移是 i+0、i+4、i+8、i+12
    int i = 0;
    for(;i+16<=n;i+=16){
        acc0 = vfmaq_f32(acc0,vld1q_f32(a+i+0),vld1q_f32(b+i+0));
        acc1 = vfmaq_f32(acc1,vld1q_f32(a+i+4),vld1q_f32(b+i+4));
        acc2 = vfmaq_f32(acc2,vld1q_f32(a+i+8),vld1q_f32(b+i+8));
        acc3 = vfmaq_f32(acc3,vld1q_f32(a+i+12),vld1q_f32(b+i+12));
    }
    // 3: 处理剩下的整 4 个（i + 4 <= n），继续用 acc0
    for(;i+4<=n;i+=4){
        acc0 = vfmaq_f32(acc0,vld1q_f32(a+i),vld1q_f32(b+i));
    }
    // 4: 树形合并 4 个累加器
    //         acc = (acc0 + acc1) + (acc2 + acc3)，用 vaddq_f32
    acc = vaddq_f32(vaddq_f32(acc0,acc1),vaddq_f32(acc2,acc3));
    // 5: ARM64 归约：float sum = vaddvq_f32(acc);
    float sum = vaddvq_f32(acc);
    // 6: 标量尾巴（i < n）
    for(;i<n;i++){
        sum = a[i]*b[i]+sum;
    }
    return sum;
}

// ------------------------------------------------------------
// 测试框架（已写好，不用改）
// 注意：NEON 是并行累加，求和顺序和标量不同，
//       所以浮点结果不会逐位相同，用相对误差判断。
// ------------------------------------------------------------
static int check(const char* name, float got, float expect)
{
    float diff = fabsf(got - expect);
    float denom = fabsf(expect) > 1e-6f ? fabsf(expect) : 1.0f;
    float rel = diff / denom;
    int ok = rel < 1e-5f;

    printf("  %-10s got=%12.6f  expect=%12.6f  rel_err=%.2e  %s\n",
           name, got, expect, rel, ok ? "OK" : "FAIL");
    return ok;
}

int main(void)
{
    enum { N = 1000 };
    float a[N];
    float b[N];

    for (int i = 0; i < N; ++i) {
        a[i] = (float)(i % 17) * 0.1f;
        b[i] = (float)(i % 13) * 0.2f;
    }

    float ref = dot_scalar(a, b, N);

    printf("=== 主用例 N=%d ===\n", N);
    int ok = 1;
    ok &= check("scalar", dot_scalar(a, b, N), ref);
    ok &= check("unrolled", dot_unrolled(a, b, N), ref);
    ok &= check("neon", dot_neon(a, b, N), ref);
    ok &= check("neon4", dot_neon4(a, b, N), ref);

    printf("\n=== 边界用例 ===\n");
    ok &= check("n=0", dot_neon(a, b, 0), 0.0f);
    ok &= check("n=1", dot_neon(a, b, 1), a[0] * b[0]);
    ok &= check("n=3", dot_neon(a, b, 3),
                a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
    ok &= check("n=17", dot_neon(a, b, 17), dot_scalar(a, b, 17));
    ok &= check("n=17/u", dot_unrolled(a, b, 17), dot_scalar(a, b, 17));
    ok &= check("n=17/n4", dot_neon4(a, b, 17), dot_scalar(a, b, 17));

    printf("\n%s\n", ok ? "*** 全部通过 ***" : "*** 有失败，检查 TODO ***");
    return ok ? 0 : 1;
}
