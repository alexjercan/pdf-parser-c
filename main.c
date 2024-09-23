#define DS_SB_IMPLEMENTATION
#define DS_IO_IMPLEMENTATION
#include "ds.h"
#include "zlib.h"

int main() {
    int result = 0;
    char *buffer = NULL;
    int buffer_len = ds_io_read_binary("./sample.pdf", &buffer);
    if (buffer_len < 0) {
        DS_LOG_ERROR("Failed to read  the file");
        return_defer(-1);
    }

    for (int i = 0; i < buffer_len; i++) {
        int start_index = 0;
        for (; i < buffer_len; i++) {
            if (strncmp("stream", buffer + i, 6) == 0) {
                start_index = i + 7;
                break;
            }
        }

        int end_index = 0;
        for (; i < buffer_len; i++) {
            if (strncmp("endstream", buffer + i, 9) == 0) {
                end_index = i;
                break;
            }
        }

        if (i >= buffer_len) {
            break;
        }

        Bytef *source = (Bytef *)(buffer + start_index);
        uLong sourceLen = (uLong)(end_index - start_index);
        uLongf destLen = sourceLen * 8;
        Bytef *dest = calloc(sizeof(Bytef), destLen);
        result = uncompress(dest, &destLen, source, sourceLen);
        if (result != Z_OK) {
            DS_LOG_ERROR("Failed to uncompress data at (%d:%d): %d", start_index, end_index, result);
            free(dest);
            continue;
        }

        ds_string_builder string_builder;
        ds_string_builder_init(&string_builder);
        char *text = (char *)dest;
        int text_len = (int)destLen;

        for (int j = 0; j < text_len; j++) {
            if (text[j] == '(') {
                j = j + 1;

                for (; j < text_len; j++) {
                    if (text[j] == ')') {
                        break;
                    }

                    ds_string_builder_appendc(&string_builder, text[j]);
                }
            }
        }

        char *plain_text = NULL;
        result = ds_string_builder_build(&string_builder, &plain_text);
        if (result != 0) {
            DS_LOG_ERROR("Could not extract text");
            free(dest);
            continue;
        }
        DS_LOG_INFO("Extracted text: '%s'", plain_text);

        free(dest);
    }

defer:
    if (buffer != NULL) {
        free(buffer);
    }
    return result;
}
