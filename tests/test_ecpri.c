/* SPDX-License-Identifier: MIT */
/* Round-trip test for the eCPRI common header. */
#include "oru/ecpri.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ecpri_hdr_t in = {
        .version       = 1,
        .concatenation = false,
        .msg_type      = ECPRI_MSG_IQ_DATA,
        .payload_size  = 240,
        .pc_id         = 0x0102,
        .seq_id        = 0x00ab,
    };

    uint8_t buf[ECPRI_HEADER_SIZE];
    int n = ecpri_hdr_encode(&in, buf, sizeof(buf));
    assert(n == (int)ECPRI_HEADER_SIZE);

    ecpri_hdr_t out;
    oru_status_t rc = ecpri_hdr_decode(buf, sizeof(buf), &out);
    assert(rc == ORU_OK);

    assert(out.version == in.version);
    assert(out.concatenation == in.concatenation);
    assert(out.msg_type == in.msg_type);
    assert(out.payload_size == in.payload_size);
    assert(out.pc_id == in.pc_id);
    assert(out.seq_id == in.seq_id);

    /* Short-buffer handling. */
    assert(ecpri_hdr_encode(&in, buf, 2) == ORU_ERR_PARAM);
    assert(ecpri_hdr_decode(buf, 2, &out) == ORU_ERR_PROTO);

    printf("test_ecpri: PASS\n");
    return 0;
}
