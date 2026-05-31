/* SPDX-License-Identifier: MIT */
/*
 * O-RAN fronthaul: C-plane (scheduling control) and U-plane (IQ data)
 * built on top of eCPRI. See docs/03-oran-fronthaul-7.2x.md.
 */
#ifndef ORU_FRONTHAUL_H
#define ORU_FRONTHAUL_H

#include "oru/types.h"

/* O-RAN section types (O-RAN.WG4.CUS-Plane). */
typedef enum {
    ORAN_SECTION_TYPE_1 = 1,  /* most DL/UL data                        */
    ORAN_SECTION_TYPE_3 = 3,  /* PRACH and mixed-numerology channels    */
} oran_section_type_t;

/* U-plane IQ compression method (udCompHdr compMeth field). */
typedef enum {
    ORAN_COMP_NONE = 0,  /* no compression, 16-bit IQ                   */
    ORAN_COMP_BFP  = 1,  /* block floating point (per-PRB exponent)     */
} oran_comp_meth_t;

/* --- C-plane: minimal Section Type 1 descriptor --- */
typedef struct {
    uint8_t  frame_id;
    uint8_t  subframe_id;
    uint8_t  slot_id;
    uint8_t  start_symbol_id;
    uint16_t start_prb;   /* startPrbc */
    uint16_t num_prb;     /* numPrbc   */
    uint16_t beam_id;
} oran_cplane_section_t;

/* --- C-plane: Section Type 3 descriptor (PRACH / mixed-numerology) ---
 * Adds the time offset and numerology fields that Section Type 1 lacks,
 * plus the per-section frequency offset used to place a PRACH occasion. */
typedef struct {
    uint8_t  frame_id;
    uint8_t  subframe_id;
    uint8_t  slot_id;
    uint8_t  start_symbol_id;
    uint16_t start_prb;        /* startPrbc                              */
    uint16_t num_prb;          /* numPrbc                                */
    uint16_t beam_id;
    uint16_t time_offset;      /* timeOffset (in samples) of the section */
    uint8_t  frame_structure;  /* frameStructure: [7:4] FFT size, [3:0] mu */
    uint16_t cp_length;        /* cpLength (cyclic prefix, in samples)   */
    int32_t  freq_offset;      /* freqOffset, signed (subcarrier units)  */
} oran_cplane_section3_t;

/* --- U-plane: a block of frequency-domain IQ for one symbol --- */
typedef struct {
    uint8_t   frame_id;
    uint8_t   subframe_id;
    uint8_t   slot_id;
    uint8_t   symbol_id;
    uint16_t  start_prb;
    uint16_t  num_prb;          /* 12 REs each */
    uint8_t   comp_meth;        /* oran_comp_meth_t (udCompHdr compMeth)  */
    uint8_t   iq_bitwidth;      /* udCompHdr bit width (e.g. 9 for BFP)   */
} oran_uplane_hdr_t;

/* --- Radio-application header ---
 * Common to every section carried in one fronthaul eCPRI message
 * (after the eCPRI common header). */
typedef struct {
    uint8_t data_direction;  /* 0 = UL (RX from antenna), 1 = DL (TX)     */
    uint8_t payload_version; /* payloadVersion (1)                        */
    uint8_t filter_index;    /* frequency-domain filter index             */
    uint8_t frame_id;
    uint8_t subframe_id;
    uint8_t slot_id;
    uint8_t start_symbol_id;
} oran_radio_app_hdr_t;

/* --- Per-section header for a U-plane section within a message --- */
typedef struct {
    uint16_t section_id;
    uint16_t start_prb;   /* startPrbu */
    uint16_t num_prb;     /* numPrbu   */
    uint8_t  comp_meth;   /* oran_comp_meth_t */
    uint8_t  iq_bitwidth;
} oran_uplane_section_hdr_t;

/* One section plus the IQ samples it carries (for encoding). */
typedef struct {
    oran_uplane_section_hdr_t hdr;
    const oru_iq16_t *iq;
    size_t            n_samples;  /* must equal hdr.num_prb * 12 */
} oran_uplane_section_t;

/* Maximum sections we will accept in a single message (parse guard). */
#define ORAN_MAX_SECTIONS 64u

/* Fronthaul subsystem lifecycle. */
oru_status_t fronthaul_init(void);
void         fronthaul_shutdown(void);

/* C-plane: encode/decode a Section Type 1 control message payload
 * (after the eCPRI common header). Returns bytes used / oru_status_t. */
int          oran_cplane_encode(const oran_cplane_section_t *s,
                                uint8_t *buf, size_t len);
oru_status_t oran_cplane_decode(const uint8_t *buf, size_t len,
                                oran_cplane_section_t *out);

/* C-plane: encode/decode a Section Type 3 control message payload
 * (PRACH / mixed-numerology). Returns bytes used / oru_status_t. */
int          oran_cplane3_encode(const oran_cplane_section3_t *s,
                                 uint8_t *buf, size_t len);
oru_status_t oran_cplane3_decode(const uint8_t *buf, size_t len,
                                 oran_cplane_section3_t *out);

/* U-plane: pack IQ samples into a U-plane payload, and parse them back.
 * The IQ payload is compressed according to hdr->comp_meth
 * (ORAN_COMP_NONE = raw 16-bit, ORAN_COMP_BFP = block floating point). */
int          oran_uplane_encode(const oran_uplane_hdr_t *h,
                                const oru_iq16_t *iq, size_t n_samples,
                                uint8_t *buf, size_t len);
oru_status_t oran_uplane_decode(const uint8_t *buf, size_t len,
                                oran_uplane_hdr_t *out_hdr,
                                oru_iq16_t *iq, size_t max_samples,
                                size_t *out_n);

/* --- U-plane: full message with a radio-app header + multiple sections ---
 *
 * Encode one fronthaul message: a single radio-application header followed
 * by `n_sections` U-plane sections, each with its own PRB range and
 * compression. Returns total bytes written, or a negative oru_status_t. */
int oran_uplane_msg_encode(const oran_radio_app_hdr_t *app,
                           const oran_uplane_section_t *sections,
                           size_t n_sections,
                           uint8_t *buf, size_t len);

/*
 * Decode a multi-section U-plane message. Fills `app`, the per-section
 * headers `sec_hdrs[]`, and concatenates all sections' IQ into `iq`
 * (section i starts at offset sec_offsets[i]). Caller supplies the array
 * capacities. Returns ORU_OK and sets *out_n_sections / *out_n_samples.
 */
typedef struct {
    oran_radio_app_hdr_t      app;
    oran_uplane_section_hdr_t sec_hdrs[ORAN_MAX_SECTIONS];
    size_t                    sec_offsets[ORAN_MAX_SECTIONS]; /* into iq[] */
    size_t                    n_sections;
    size_t                    n_samples;       /* total across sections   */
} oran_uplane_msg_t;

oru_status_t oran_uplane_msg_decode(const uint8_t *buf, size_t len,
                                    oran_uplane_msg_t *out,
                                    oru_iq16_t *iq, size_t max_samples);

#endif /* ORU_FRONTHAUL_H */
