/*
 *   sbloomfilter.c - simple Bloom Filter
 *   (c) Tatsuya Mori <valdzone@gmail.com>
 */

#include "ruby.h"
#include "crc32.h"

static VALUE cBloomFilter;

struct BloomFilter {
    int m; /* # of buckets in a bloom filter */
    int b; /* # of bits in a bloom filter bucket */
    int k; /* # of hash functions */
    int s; /* # seed of hash functions */
    int r; /* # raise on bucket overflow? */
    int num_set; /* # of set bits */
    unsigned char *ptr; /* bits data */
};

void bits_free(struct BloomFilter *bf) {
    ruby_xfree(bf->ptr);
}


void bucket_unset(struct BloomFilter *bf, int index) {
    int byte_offset = (index * bf->b) / 8;
    int bit_offset = (index * bf->b) % 8;
    unsigned int c = bf->ptr[byte_offset];
    c += bf->ptr[byte_offset + 1] << 8;
    unsigned int mask = ((1 << bf->b) - 1) << bit_offset;
    if ((c & mask) == 0) {
      // do nothing
    } else {
        bf->ptr[byte_offset] -= (1 << bit_offset) & ((1 << 8) - 1);
        bf->ptr[byte_offset + 1] -= ((1 << bit_offset) & ((1 << 16) - 1)) >> 8;
    }
    
}

void bucket_set(struct BloomFilter *bf, int index) {
    int byte_offset = (index * bf->b) / 8;
    int bit_offset = (index * bf->b) % 8;
    unsigned int c = bf->ptr[byte_offset];
    c += bf->ptr[byte_offset + 1] << 8;
    unsigned int mask = ((1 << bf->b) - 1) << bit_offset;
    if ((c & mask) == mask) {
        if (bf->r == 1) rb_raise(rb_eRuntimeError, "bucket got filled up");
    } else {
        bf->ptr[byte_offset] += (1 << bit_offset) & ((1 << 8) - 1);
        bf->ptr[byte_offset + 1] += ((1 << bit_offset) & ((1 << 16) - 1)) >> 8;
    }
    
}

int bucket_check(struct BloomFilter *bf, int index) {
    int byte_offset = (index * bf->b) / 8;
    int bit_offset = (index * bf->b) % 8;
    unsigned int c = bf->ptr[byte_offset];
    c += bf->ptr[byte_offset + 1] << 8;

    unsigned int mask = ((1 << bf->b) - 1) << bit_offset;
    return (c & mask) >> bit_offset;
}

int bucket_get(struct BloomFilter *bf, int index) {
  int byte_offset = (index * bf->b) / 8;
  int bit_offset = (index * bf->b) % 8;
  unsigned int c = bf->ptr[byte_offset];
  c += bf->ptr[byte_offset + 1] << 8;

  unsigned int mask = ((1 << bf->b) - 1) << bit_offset;
  return (c & mask) >> bit_offset;
}

static VALUE bf_s_new(int argc, VALUE *argv, VALUE self) {
    struct BloomFilter *bf;
    VALUE arg1, arg2, arg3, arg4, arg5, obj;
    int m, k, s, b, r, bytes;

    obj = Data_Make_Struct(self, struct BloomFilter, NULL, bits_free, bf);

    /* default = Fugou approach :-) */
    arg1 = INT2FIX(100000000);
    arg2 = INT2FIX(4);
    arg3 = INT2FIX(0);
    arg4 = INT2FIX(1);
    arg5 = INT2FIX(0);

    switch (argc) {
        case 5:
	    if (argv[4] == Qtrue) {
		    arg5 = INT2FIX(1);
	    }
        case 4:
	    arg4 = argv[3];
        case 3:
	    arg3 = argv[2];
        case 2:
	    arg2 = argv[1];
        case 1:
	    arg1 = argv[0];
	    break;
    }

    m = FIX2INT(arg1);
    k = FIX2INT(arg2);
    s = FIX2INT(arg3);
    b = FIX2INT(arg4);
    r = FIX2INT(arg5);

    if (b < 1 || b > 8)
        rb_raise(rb_eArgError, "bucket size");
    if (m < 1)
        rb_raise(rb_eArgError, "array size");
    if (k < 1)
        rb_raise(rb_eArgError, "hash length");
    if (s < 0)
        rb_raise(rb_eArgError, "random seed");

    bf->b = b;
    bf->m = m;
    bf->k = k;
    bf->s = s;
    bf->r = r;
    bf->num_set = 0;

    bytes = ((m * b) + 15) / 8;
    bf->ptr = ALLOC_N(unsigned char, bytes);

    /* initialize the bits with zeros */
    memset(bf->ptr, 0, bytes);
    rb_iv_set(obj, "@hash_value", rb_hash_new());

    return obj;
}

static VALUE bf_m(VALUE self) {
    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);
    return INT2FIX(bf->m);
}

static VALUE bf_k(VALUE self) {
    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);
    return INT2FIX(bf->k);
}

static VALUE bf_b(VALUE self) {
    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);
    return INT2FIX(bf->b);
}

static VALUE bf_r(VALUE self) {
    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);
    return bf->r == 0 ? Qfalse : Qtrue;
}

static VALUE bf_num_set(VALUE self) {
    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);
    return INT2FIX(bf->num_set);
}

static VALUE bf_insert(VALUE self, VALUE key) {
    int index, seed;
    int i, len, m, k, s;
    char *ckey;

    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);

    Check_Type(key, T_STRING);
    ckey = STR2CSTR(key);
    len = (int) (RSTRING(key)->len); /* length of the string in bytes */

    m = bf->m;
    k = bf->k;
    s = bf->s;

    for (i = 0; i <= k - 1; i++) {
        /* seeds for hash functions */
        seed = i + s;

        /* hash */
        index = (int) (crc32((unsigned int) (seed), ckey, len) % (unsigned int) (m));

        /*  set a bit at the index */
        bucket_set(bf, index);
    }

    bf->num_set += 1;
    return Qnil;
}

static VALUE bf_delete(VALUE self, VALUE key) {
    int index, seed;
    int i, len, m, k, s;
    char *ckey;

    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);

    Check_Type(key, T_STRING);
    ckey = STR2CSTR(key);
    len = (int) (RSTRING(key)->len); /* length of the string in bytes */

    m = bf->m;
    k = bf->k;
    s = bf->s;

    for (i = 0; i <= k - 1; i++) {
        /* seeds for hash functions */
        seed = i + s;

        /* hash */
        index = (int) (crc32((unsigned int) (seed), ckey, len) % (unsigned int) (m));

        /*  set a bit at the index */
        bucket_unset(bf, index);
    }

    bf->num_set += 1;
    return Qnil;
}


static VALUE bf_include(VALUE self, VALUE key) {
    int index, seed;
    int i, len, m, k, s;
    char *ckey;

    struct BloomFilter *bf;
    Data_Get_Struct(self, struct BloomFilter, bf);

    Check_Type(key, T_STRING);
    ckey = STR2CSTR(key);
    len = (int) (RSTRING(key)->len); /* length of the string in bytes */

    m = bf->m;
    k = bf->k;
    s = bf->s;

    for (i = 0; i <= k - 1; i++) {
        /* seeds for hash functions */
        seed = i + s;

        /* hash */
        index = (int) (crc32((unsigned int) (seed), ckey, len) % (unsigned int) (m));

        /* check the bit at the index */
        if (!bucket_check(bf, index))
            return Qfalse; /* i.e., it is a new entry ; escape the loop */
    }

    return Qtrue;
}

static VALUE bf_to_s(VALUE self) {
    struct BloomFilter *bf;
    unsigned char *ptr;
    int i;
    VALUE str;

    Data_Get_Struct(self, struct BloomFilter, bf);
    str = rb_str_new(0, bf->m);

    ptr = (unsigned char *) RSTRING(str)->ptr;
    for (i = 0; i < bf->m; i++)
        *ptr++ = bucket_get(bf, i) ? '1' : '0';

    return str;
}

void Init_sbloomfilter(void) {
    cBloomFilter = rb_define_class("BloomFilter", rb_cObject);
    rb_define_singleton_method(cBloomFilter, "new", bf_s_new, -1);
    rb_define_method(cBloomFilter, "m", bf_m, 0);
    rb_define_method(cBloomFilter, "k", bf_k, 0);
    rb_define_method(cBloomFilter, "b", bf_b, 0);
    rb_define_method(cBloomFilter, "r", bf_r, 0);
    rb_define_method(cBloomFilter, "num_set", bf_num_set, 0);
    rb_define_method(cBloomFilter, "insert", bf_insert, 1);
    rb_define_method(cBloomFilter, "delete", bf_delete, 1);
    rb_define_method(cBloomFilter, "include?", bf_include, 1);
    rb_define_method(cBloomFilter, "to_s", bf_to_s, 0);

    /* functions that have not been implemented, yet */

    //  rb_define_method(cBloomFilter, "clear", bf_clear, 0);
    //  rb_define_method(cBloomFilter, "&", bf_and, 1);
    //  rb_define_method(cBloomFilter, "|", bf_or, 1);
    //  rb_define_method(cBloomFilter, "<=>", bf_cmp, 1);
}
