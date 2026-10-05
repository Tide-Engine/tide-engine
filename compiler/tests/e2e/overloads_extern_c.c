// The C functions overloads_extern.tide declares.

typedef struct pair {
    float a;
    int b;
} pair;

float c_scale(const float value, const float by)
{
    return value * by;
}

float c_mix_floats(const float a, const float b)
{
    return a + b;
}

int c_mix_ints(const int a, const int b)
{
    return a * b;
}

float c_pair_total(const pair *p)
{
    return p->a + (float)p->b;
}

static int calls;

int c_count(void)
{
    return ++calls;
}

int c_digits(const int tens, const int ones)
{
    return tens * 10 + ones;
}
