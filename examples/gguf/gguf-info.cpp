#include "ggml.h"

#include <cstdio>
#include <cinttypes>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>

#undef MIN
#undef MAX
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

template <typename T>
static std::string to_string(const T &val)
{
    std::stringstream ss;
    ss << val;
    return ss.str();
}

// read and create ggml_context containing the tensors and their data
bool gguf_info(const std::string &fname)
{
    struct ggml_context *ctx_data = NULL;

    struct gguf_init_params params = {
        /*.no_alloc = */ false,
        /*.ctx      = */ &ctx_data,
    };

    struct gguf_context *ctx = gguf_init_from_file(fname.c_str(), params);

    std::map<std::string, std::string> kv_map;

    fprintf(stdout, "version:      %d\n", gguf_get_version(ctx));
    fprintf(stdout, "alignment:   %zu\n", gguf_get_alignment(ctx));
    fprintf(stdout, "data offset: %zu\n", gguf_get_data_offset(ctx));

    const int n_kv = gguf_get_n_kv(ctx);

    for (int i = 0; i < n_kv; ++i)
    {
        const char *key = gguf_get_key(ctx, i);
        const auto key_type = gguf_get_kv_type(ctx, i);

        switch (key_type)
        {
        case GGUF_TYPE_UINT8:
            kv_map[key] = std::to_string(gguf_get_val_u8(ctx, i));
            break;
        case GGUF_TYPE_INT8:
            kv_map[key] = std::to_string(gguf_get_val_i8(ctx, i));
            break;
        case GGUF_TYPE_UINT16:
            kv_map[key] = std::to_string(gguf_get_val_u16(ctx, i));
            break;
        case GGUF_TYPE_INT16:
            kv_map[key] = std::to_string(gguf_get_val_i16(ctx, i));
            break;
        case GGUF_TYPE_UINT32:
            kv_map[key] = std::to_string(gguf_get_val_u32(ctx, i));
            break;
        case GGUF_TYPE_INT32:
            kv_map[key] = std::to_string(gguf_get_val_i32(ctx, i));
            break;
        case GGUF_TYPE_UINT64:
            kv_map[key] = std::to_string(gguf_get_val_u64(ctx, i));
            break;
        case GGUF_TYPE_INT64:
            kv_map[key] = std::to_string(gguf_get_val_i64(ctx, i));
            break;
        case GGUF_TYPE_FLOAT32:
            kv_map[key] = std::to_string(gguf_get_val_f32(ctx, i));
            break;
        case GGUF_TYPE_FLOAT64:
            kv_map[key] = std::to_string(gguf_get_val_f64(ctx, i));
            break;
        case GGUF_TYPE_BOOL:
            kv_map[key] = std::to_string(gguf_get_val_bool(ctx, i));
            break;
        case GGUF_TYPE_STRING:
            kv_map[key] = gguf_get_val_str(ctx, i);
            break;
        case GGUF_TYPE_ARRAY:
            // kv_map[key] = std::to_string(gguf_get_val_arr_size(ctx, i));
            kv_map[key] = "[array]";
            break;
        default:
            fprintf(stdout, "'%s' unknown key type: %d\n", key, key_type);
            break;
        }
    }

    for (const auto &kv : kv_map)
    {
        fprintf(stdout, "%s: %s\n", kv.first.c_str(), kv.second.c_str());
    }

    fprintf(stdout, "ctx_data size: %zu\n", ggml_get_mem_size(ctx_data));

    ggml_free(ctx_data);
    gguf_free(ctx);

    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stdout, "usage: %s data.gguf\n", argv[0]);
        return -1;
    }

    const std::string fname(argv[1]);

    GGML_ASSERT(gguf_info(fname) && "failed to read gguf file");

    return 0;
}
