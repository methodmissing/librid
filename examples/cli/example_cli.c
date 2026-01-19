#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rid/rid.h"

#define RID_MESSAGE_LENGTH 25
#define RID_BTMON_LENGTH 27

static int
hex_to_bytes(const char *hex, uint8_t *bytes, size_t max_length)
{
    size_t hex_length = strlen(hex);
    if (hex_length % 2 != 0) {
        return -1;
    }

    size_t byte_length = hex_length / 2;
    if (byte_length > max_length) {
        return -1;
    }

    for (size_t i = 0; i < byte_length; ++i) {
        unsigned int byte;
        if (sscanf(hex + 2 * i, "%2x", &byte) != 1) {
            return -1;
        }
        bytes[i] = (uint8_t)byte;
    }

    return (int)byte_length;
}

static void
strip_newline(char *str)
{
    size_t length = strlen(str);
    if (length > 0 && str[length - 1] == '\n') {
        str[length - 1] = '\0';
    }
}

static int
decode_and_print(const char *hex_string)
{
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));
    int length = hex_to_bytes(hex_string, buffer, sizeof(buffer));
    if (length < 0) {
        fprintf(stderr, "Error: Invalid hex string\n");
        return 1;
    }

    uint8_t *message = buffer;
    int counter = -1;

    if (length == RID_BTMON_LENGTH) {
        /* btmon format: app_code (1) + counter (1) + message (25) */
        if (buffer[0] != RID_TRANSPORT_BLUETOOTH_APP_CODE) {
            fprintf(stderr, "Error: Invalid application code 0x%02X, expected 0x%02X\n",
                    buffer[0], RID_TRANSPORT_BLUETOOTH_APP_CODE);
            return 1;
        }
        counter = buffer[1];
        message = buffer + 2;
        length -= 2;
    }

    /* Determine expected length based on message type */
    int expected_length = RID_MESSAGE_LENGTH;
    if (rid_message_get_type(message) == RID_MESSAGE_TYPE_MESSAGE_PACK) {
        if (length < 3) {
            fprintf(stderr, "Error: Message pack too short\n");
            return 1;
        }
        /* header (1) + size (1) + count (1) + messages (n * 25) */
        int count =
            rid_message_pack_get_message_count((rid_message_pack_t *)message);
        expected_length = 3 + count * RID_MESSAGE_SIZE;

        /* Verify alignment */
        if ((length - 3) % RID_MESSAGE_SIZE != 0) {
            fprintf(stderr,
                "Error: Invalid message pack length %d (alignment error)\n",
                length);
            return 1;
        }
    }

    if (length != expected_length) {
        if (rid_message_get_type(message) == RID_MESSAGE_TYPE_MESSAGE_PACK) {
            fprintf(stderr, "Error: Message pack length %d does not match count\n",
                length);
        } else {
            fprintf(stderr, "Error: Expected %d or %d bytes, got %d\n",
                RID_MESSAGE_LENGTH, RID_BTMON_LENGTH,
                length + (counter >= 0 ? 2 : 0));
        }
        return 1;
    }

    int rc = rid_message_validate(message);
    if (rc < 0) {
        fprintf(stderr, "Error: %s\n", rid_error_to_string(rc));
        return 1;
    }

    char json[4096];
    rc = rid_message_to_json(message, json, sizeof(json));
    if (rc < 0) {
        fprintf(stderr, "Error: %s\n", rid_error_to_string(rc));
        return 1;
    }

    /* Output with optional counter prefix */
    if (counter >= 0) {
        /* Insert counter after opening brace */
        printf("{\"counter\": %d, %s\n", counter, json + 1);
    } else {
        printf("%s\n", json);
    }
    fflush(stdout);

    return 0;
}

int
main(int argc, char *argv[])
{
    char hex_input[512];
    const char *hex_string = NULL;

    /* Handle --help / -h */
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: %s [<hex_string>]\n", argv[0]);
        return 0;
    }

    /* Determine hex_string source */
    if (argc == 2) {
        hex_string = argv[1];
    } else if (argc > 2) {
        fprintf(stderr, "Usage: %s [<hex_string>]\n", argv[0]);
        return 1;
    }

    /* If hex_string provided, decode and exit */
    if (hex_string != NULL) {
        return decode_and_print(hex_string);
    }

    /* Read hex strings from stdin in a loop */
    while (fgets(hex_input, sizeof(hex_input), stdin) != NULL) {
        strip_newline(hex_input);
        if (hex_input[0] == '\0') {
            continue;
        }
        decode_and_print(hex_input);
    }

    return 0;
}
