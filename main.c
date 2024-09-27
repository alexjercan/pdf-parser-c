#include <assert.h>
#define DS_DA_IMPLEMENTATION
#define DS_SS_IMPLEMENTATION
#define DS_SB_IMPLEMENTATION
#define DS_IO_IMPLEMENTATION
#include "ds.h"
#include "zlib.h"

typedef enum filter_kind {
    filter_flate_decode,
    filter_dct_decode
} filter_kind;

typedef enum object_kind {
    object_boolean,
    object_real,
    object_int,
    object_string,
    object_name,
    object_array,
    object_dictionary,
    object_stream,
    object_null,
    object_indirect,
    object_pointer,
} object_kind;

typedef int boolean;

typedef struct indirect_object {
    int object_number;
    int generation_number;
    ds_dynamic_array objects; /* object_t */
} indirect_object;

typedef struct pointer_object {
    int object_number;
    int generation_number;
} pointer_object;

typedef struct object {
    object_kind kind;
    union {
        boolean bool;
        float real;
        int integer;
        char *string;
        char *name;
        ds_dynamic_array array; /* object_t */
        ds_dynamic_array dictionary; /* object_kv */
        ds_string_slice stream;
        indirect_object object;
        pointer_object pointer;
    };
} object_t;

typedef struct object_kv {
    char *name;
    object_t object;
} object_kv;

typedef struct pdf {
    ds_dynamic_array objects; /* indirect_object */
} pdf_t;

void skip_comments(ds_string_slice *slice) {
    ds_string_slice line;
    while (ds_string_slice_starts_with(slice, "%") == 0) {
        ds_string_slice_tokenize(slice, '\n', &line);
    }
}

int isnamechar(char c) {
    return isalpha(c) || isdigit(c) || c == '.' || c == '+' || c == '-';
}

int isnumber(char c) {
    return isdigit(c) || c == '-' || c == '.';
}

int ispointer(ds_string_slice *slice) {
    // we ignore floats for now, also negative numbers

    ds_string_slice token;
    ds_string_slice tmp_slice = *slice;
    ds_string_slice_take_while_pred(&tmp_slice, isnumber, &token);
    ds_string_slice_trim_left_ws(&tmp_slice);
    if (ds_string_slice_starts_with_pred(&tmp_slice, isnumber)) {
        ds_string_slice_take_while_pred(&tmp_slice, isnumber, &token);
        ds_string_slice_trim_left_ws(&tmp_slice);
        if (ds_string_slice_starts_with(&tmp_slice, "R") == 0) {
            return 1;
        } else {
            return 0;
        }
    }

    return 0;
}

int parse_direct_object(ds_string_slice *slice, object_t *object);

int parse_dictionary_object(ds_string_slice *slice, object_t *object) {
    int result = 0;

    object->kind = object_dictionary;
    ds_dynamic_array_init(&object->dictionary, sizeof(object_kv));

    ds_string_slice_trim_left(slice, '<');

    while (1) {
        object_kv obj_kv;

        if (ds_string_slice_empty(slice)) {
            DS_LOG_ERROR("Expected a name or `>>` but found EOF");
            return_defer(1);
        }

        ds_string_slice_trim_left_ws(slice);
        if (ds_string_slice_starts_with(slice, ">>") == 0) {
            ds_string_slice_trim_left(slice, '>');
            break;
        } else if (ds_string_slice_starts_with(slice, "/") == 0) {
            ds_string_slice token;
            ds_string_slice_step(slice, 1);
            ds_string_slice_take_while_pred(slice, isnamechar, &token);
            ds_string_slice_to_owned(&token, &obj_kv.name);
        } else {
            DS_LOG_ERROR("Expected a name or `>>`");
            return_defer(1);
        }

        if (parse_direct_object(slice, &obj_kv.object) != 0) {
            DS_LOG_ERROR("Could not parse object in dictionary");
            return_defer(1);
        }

        ds_dynamic_array_append(&object->dictionary, &obj_kv);
    }

defer:
    return result;
}

int parse_string_object(ds_string_slice *slice, object_t *object) {
    int result = 0;
    char start = *slice->str;
    char end = start == '<' ? '>' : ')';

    object->kind = object_string;

    ds_string_slice_step(slice, 1); // Remove the <

    ds_string_slice tmp_slice = *slice;
    tmp_slice.len = 0;

    while (*slice->str != end) {
        ds_string_slice_step(slice, 1);
        tmp_slice.len += 1;
    }

    ds_string_slice_to_owned(&tmp_slice, &object->string);
    ds_string_slice_step(slice, 1); // Remove the >

defer:
    return result;
}

int parse_stream_object(ds_string_slice *slice, object_t *object) {
    char *tmp;
    ds_string_slice token;
    int result = 0;

    object->kind = object_stream;

    ds_string_slice line;
    if (ds_string_slice_tokenize(slice, '\n', &line) != 0) {
        DS_LOG_ERROR("Expected a line but found EOF");
        return_defer(1);
    }

    object->stream = *slice;
    object->stream.len = 0;

    while (ds_string_slice_starts_with(slice, "endstream") != 0) {
        ds_string_slice_step(slice, 1);
        object->stream.len += 1;
    }

    ds_string_slice_step(slice, strlen("endstream"));

defer:
    return result;
}

int parse_array_object(ds_string_slice *slice, object_t *object) {
    char *tmp;
    ds_string_slice token;
    int result = 0;

    object->kind = object_array;
    ds_dynamic_array_init(&object->array, sizeof(object_t));

    ds_string_slice_trim_left(slice, '[');

    while (1) {
        object_t obj;

        if (ds_string_slice_empty(slice)) {
            DS_LOG_ERROR("Expected an object or `]` but found EOF");
            return_defer(1);
        }

        ds_string_slice_trim_left_ws(slice);
        if (ds_string_slice_starts_with(slice, "]") == 0) {
            ds_string_slice_trim_left(slice, ']');
            break;
        }

        if (parse_direct_object(slice, &obj) != 0) {
            DS_LOG_ERROR("Could not parse object in dictionary");
            return_defer(1);
        }

        ds_dynamic_array_append(&object->array, &obj);
    }

defer:
    return result;
}

int parse_name_object(ds_string_slice *slice, object_t *object) {
    char *tmp;
    ds_string_slice token;
    int result = 0;

    object->kind = object_name;

    ds_string_slice_step(slice, 1); // Remove the /

    ds_string_slice_take_while_pred(slice, isnamechar, &token);
    ds_string_slice_to_owned(&token, &tmp);
    object->name = tmp;
    ds_string_slice_trim_left_ws(slice);

defer:
    return result;
}

int parse_pointer_object(ds_string_slice *slice, object_t *object) {
    char *tmp;
    ds_string_slice token;
    int result = 0;

    object->kind = object_pointer;

    ds_string_slice_take_while_pred(slice, isnumber, &token);
    ds_string_slice_to_owned(&token, &tmp);
    object->pointer.object_number = atoi(tmp);
    free(tmp);
    ds_string_slice_trim_left_ws(slice);

    ds_string_slice_take_while_pred(slice, isnumber, &token);
    ds_string_slice_to_owned(&token, &tmp);
    object->pointer.generation_number = atoi(tmp);
    free(tmp);
    ds_string_slice_trim_left_ws(slice);

    ds_string_slice_step(slice, 1); // Remove the R

defer:
    return result;
}

int parse_number_object(ds_string_slice *slice, object_t *object) {
    char *tmp;
    ds_string_slice token;
    int result = 0;

    ds_string_slice_take_while_pred(slice, isnumber, &token);
    ds_string_slice_to_owned(&token, &tmp);

    char *tmp2 = tmp;
    int is_int = 1;
    for (char c = *tmp2; c != '\0'; c = *tmp2++) {
        is_int = is_int && (c != '.');
    }

    if (is_int) {
        object->kind = object_int;
        object->integer = atoi(tmp);
    } else {
        object->kind = object_real;
        object->real = atof(tmp);
    }

defer:
    return result;
}

int parse_boolean_object(ds_string_slice *slice, object_t *object) {
    char *tmp;
    ds_string_slice token;
    int result = 0;

    object->kind = object_boolean;

    ds_string_slice_take_while_pred(slice, isnamechar, &token);
    ds_string_slice_to_owned(&token, &tmp);

    object->bool = strncmp(tmp, "true", 4) == 0;

defer:
    return result;
}

int parse_direct_object(ds_string_slice *slice, object_t *object) {
    int result = 0;

    ds_string_slice_trim_left_ws(slice);
    if (ds_string_slice_starts_with(slice, "<<") == 0) {
        if (parse_dictionary_object(slice, object) != 0) {
            DS_LOG_ERROR("Failed to parse dictionary");
            return_defer(1);
        }
    } else if (ds_string_slice_starts_with(slice, "<") == 0 || ds_string_slice_starts_with(slice, "(") == 0) {
        if (parse_string_object(slice, object) != 0) {
            DS_LOG_ERROR("Failed to parse string");
            return_defer(1);
        }
    } else if (ds_string_slice_starts_with(slice, "stream") == 0) {
        if (parse_stream_object(slice, object) != 0) {
            DS_LOG_ERROR("Failed to parse stream");
            return_defer(1);
        }
    } else if (ds_string_slice_starts_with(slice, "[") == 0) {
        if (parse_array_object(slice, object) != 0) {
            DS_LOG_ERROR("Failed to parse array");
            return_defer(1);
        }
    } else if (ds_string_slice_starts_with(slice, "/") == 0) {
        if (parse_name_object(slice, object) != 0) {
            DS_LOG_ERROR("Failed to parse name");
            return_defer(1);
        }
    } else {
        if (ds_string_slice_starts_with_pred(slice, isnumber)) {
            if (ispointer(slice)) {
                if (parse_pointer_object(slice, object) != 0) {
                    DS_LOG_ERROR("Failed to parse pointer");
                    return_defer(1);
                }
            } else {
                if (parse_number_object(slice, object) != 0) {
                    DS_LOG_ERROR("Failed to parse number");
                    return_defer(1);
                }
            }
        } else {
            if (parse_boolean_object(slice, object) != 0) {
                DS_LOG_ERROR("Failed to parse boolean");
                return_defer(1);
            }
        }
    }

defer:
    return result;
}

int parse_indirect_object(ds_string_slice *slice, indirect_object *object) {
    int result = 0;

    ds_string_slice line;
    if (ds_string_slice_tokenize(slice, '\n', &line) != 0) {
        DS_LOG_ERROR("Expected a line but found EOF");
        return_defer(1);
    }

    // we must have `x y obj`
    ds_string_slice token;
    char *word = NULL;
    ds_dynamic_array_init(&object->objects, sizeof(object_t));

    // could use ws stuff
    ds_string_slice_tokenize(&line, ' ', &token);
    ds_string_slice_to_owned(&token, &word);
    object->object_number = atoi(word);
    free(word);

    ds_string_slice_tokenize(&line, ' ', &token);
    ds_string_slice_to_owned(&token, &word);
    object->generation_number = atoi(word);
    free(word);

    // we should have a direct object (or more)
    while (1) {
        if (ds_string_slice_empty(slice)) {
            DS_LOG_ERROR("Expected a direct object or `endobj` keyword but found EOF");
            return_defer(1);
        }

        ds_string_slice_trim_left_ws(slice);
        if (ds_string_slice_starts_with(slice, "endobj") == 0) {
            ds_string_slice_tokenize(slice, '\n', &line);
            break;
        } else {
            object_t obj;
            parse_direct_object(slice, &obj);
            ds_dynamic_array_append(&object->objects, &obj);
        }
    }

defer:
    return result;
}

int parse_pdf(char *buffer, int buffer_len, pdf_t *pdf) {
    int result = 0;
    indirect_object object = {0};
    ds_string_slice slice, line;
    ds_string_slice_init(&slice, buffer, buffer_len);
    ds_dynamic_array_init(&pdf->objects, sizeof(object_t));

    while (1) {
        skip_comments(&slice);
        ds_string_slice_trim_left_ws(&slice);
        if (ds_string_slice_empty(&slice)) {
            break;
        }

        if (ds_string_slice_starts_with(&slice, "xref") == 0) {
            break;
        } else {
            if (parse_indirect_object(&slice, &object) == 0) {
                ds_dynamic_array_append(&pdf->objects, &object);
            }
        }
    }

defer:
    return result;
}

filter_kind get_filter_kind(ds_dynamic_array dictionary /* object_kv */) {
    for (int i = 0; i < dictionary.count; i++) {
        object_kv kv = {0};
        ds_dynamic_array_get(&dictionary, i, &kv);

        if (strncmp(kv.name, "Filter", 6) == 0) {
            assert(kv.object.kind == object_name);

            if (strncmp(kv.object.name, "FlateDecode", 11) == 0) {
                return filter_flate_decode;
            } else if (strncmp(kv.object.name, "DCTDecode", 9) == 0) {
                return filter_dct_decode;
            }
        }
    }

    return 2;
}

void show_text(ds_string_slice stream) {
    Bytef *source = (Bytef *)(stream.str);
    uLong sourceLen = (uLong)(stream.len);
    uLongf destLen = sourceLen * 8;
    Bytef *dest = calloc(sizeof(Bytef), destLen);
    int result = uncompress(dest, &destLen, source, sourceLen);
    if (result != Z_OK) {
        DS_LOG_ERROR("Failed to uncompress data at : %d", result);
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
    }
    DS_LOG_INFO("Extracted text: '%s'", plain_text);
}

void show_image(ds_string_slice stream) {
    ds_io_write_binary("image.jpeg", stream.str, stream.len);
}

int main() {
    int result = 0;
    pdf_t pdf = {0};
    char *buffer = NULL;
    int buffer_len = ds_io_read_binary("./sample.pdf", &buffer);
    if (buffer_len < 0) {
        DS_LOG_ERROR("Failed to read the file");
        return_defer(-1);
    }

    result = parse_pdf(buffer, buffer_len, &pdf);
    if (result != 0) {
        DS_LOG_ERROR("Failed to parse the buffer: %d", result);
        return_defer(-1);
    }

    for (int i = 0; i < pdf.objects.count; i++) {
        indirect_object object = {0};
        ds_dynamic_array_get(&pdf.objects, i, &object);

        int is_stream = 0;
        for (int j = 0; j < object.objects.count; j++) {
            object_t obj = {0};
            ds_dynamic_array_get(&object.objects, j, &obj);
            if (obj.kind == object_stream) {
                is_stream = 1;
                break;
            }
        }

        if (is_stream == 1) {
            object_t dictionary = {0};
            ds_dynamic_array_get(&object.objects, 0, &dictionary);
            assert(dictionary.kind == object_dictionary);

            object_t stream = {0};
            ds_dynamic_array_get(&object.objects, 1, &stream);
            assert(stream.kind == object_stream);

            filter_kind kind = get_filter_kind(dictionary.dictionary);

            switch (kind) {
            case filter_flate_decode: show_text(stream.stream); break;
            case filter_dct_decode: show_image(stream.stream); break;
            }
        }
    }

defer:
    if (buffer != NULL) {
        free(buffer);
    }
    return result;
}
