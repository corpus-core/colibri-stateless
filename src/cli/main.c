#include <stdio.h>
#include <stdlib.h>
#include "../util/bytes.h"
#include "../util/ssz.h"


ssz_def_t CA[] = {
    SSZ_UINT("fix", 1),
    SSZ_LIST("dynamic",SSZ_BYTE)
};

ssz_def_t CB[] = {
    SSZ_UINT("fix", 1),
    SSZ_LIST("dynamic",SSZ_BYTE),
    SSZ_CONTAINER("sub",CA)
};
ssz_def_t CB_CONTAINER = SSZ_CONTAINER("",CB);
ssz_def_t CA_CONTAINER = SSZ_CONTAINER("",CA);


static bytes_t read_from_stdin() {
 unsigned char buffer[1024];
    size_t bytesRead;
    bytes_buffer_t data = {0};

    while ((bytesRead = fread(buffer, 1, 1024, stdin)) > 0) 
       buffer_append(&data,bytes(buffer,bytesRead));

    return data.data;
}

static bytes_t read_from_file(const char* filename) {
    unsigned char buffer[1024];
    size_t bytesRead;
    bytes_buffer_t data = {0};

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    while ((bytesRead = fread(buffer, 1, 1024, file)) > 0) 
        buffer_append(&data, bytes(buffer, bytesRead));

    fclose(file);
    return data.data;
}

int main(int argc, char *argv[]) {

    bytes_t data = read_from_file("testdata.ssz");// read_from_stdin();
    ssz_ob_t in_data = ssz_ob(CB_CONTAINER,data);
    ssz_ob_t res = in_data;
        // Iterate over each argument
    for (int i = 1; i < argc; i++) 
        res = ssz_get(&res, argv[i]);

    if (res.bytes.data==NULL)
       fprintf(stderr, "Nothing found!\n");
    else
       ssz_dump(stdout, res, false, 0);

       printf("\n_________\n");
       ssz_buffer_t buffer = {0};
       buffer.def = &CB_CONTAINER;

       ssz_add_uint8(&buffer, ssz_get(&res,"fix").bytes.data[0]);
       ssz_add_bytes(&buffer, "dynamic", ssz_get(&res,"dynamic").bytes);

       ssz_ob_t sub = ssz_get(&res,"sub");        
       ssz_buffer_t sub_buffer = {0};
       sub_buffer.def = &CA_CONTAINER;
       ssz_add_uint8(&sub_buffer, ssz_get(&sub,"fix").bytes.data[0]);
       ssz_add_bytes(&sub_buffer, "dynamic", ssz_get(&sub,"dynamic").bytes);
       ssz_ob_t sub_new = ssz_buffer_to_bytes(&sub_buffer);
       ssz_add_bytes(&buffer,"sub",sub_new.bytes);
       free(sub_new.bytes.data);

       ssz_ob_t out_bytes = ssz_buffer_to_bytes(&buffer);
       ssz_dump(stdout, out_bytes, false, 0);
       free(out_bytes.bytes. data);










//       fwrite(res.bytes.data, 1, res.bytes.len, stdout);
    return 0;
}
