#include "nightfall.h"
#include "nf_libc.h"

void nf_link_init(nf_link *link, nf_state *state)
{
    if (!link) return;
    memset(link, 0, sizeof *link);
    link->state = state;
}

static int project_command(nf_link *link, const nf_packet *request, nf_packet *reply)
{
    const uint8_t *p = request->payload;
    uint8_t slot, part;
    size_t offset;
    if (!request->length || p[0] >= NF_PROJECT_SLOTS) return -1;
    slot = p[0];
    switch (request->command) {
    case NF_CMD_PROJECT_EXPORT:
        if (request->length != 2u || p[1] >= NF_PROJECT_CHUNKS ||
            link->uploading) return -1;
        if (!nf_export_project(link->state, slot, link->staging,
                               sizeof link->staging)) return -3;
        part = p[1];
        offset = (size_t)part * NF_PROJECT_CHUNK_SIZE;
        reply->length = NF_PROJECT_CHUNK_SIZE + 2u;
        reply->payload[1] = part;
        memcpy(reply->payload + 2u, link->staging + offset, NF_PROJECT_CHUNK_SIZE);
        return 0;
    case NF_CMD_PROJECT_IMPORT_BEGIN:
        if (request->length != 1u || link->state->playing) return -1;
        link->uploading = 1u;
        link->upload_slot = slot;
        link->received = 0u;
        return 0;
    case NF_CMD_PROJECT_IMPORT_CHUNK:
        if (request->length != NF_PROJECT_CHUNK_SIZE + 2u ||
            !link->uploading || slot != link->upload_slot ||
            p[1] >= NF_PROJECT_CHUNKS) return -1;
        part = p[1];
        offset = (size_t)part * NF_PROJECT_CHUNK_SIZE;
        memcpy(link->staging + offset, p + 2u, NF_PROJECT_CHUNK_SIZE);
        link->received |= (uint8_t)(1u << part);
        reply->length = 2u;
        reply->payload[1] = link->received;
        return 0;
    case NF_CMD_PROJECT_IMPORT_COMMIT:
        if (request->length != 1u || !link->uploading ||
            slot != link->upload_slot ||
            link->received != ((1u << NF_PROJECT_CHUNKS) - 1u)) return -1;
        /* Import validates the blob CRC and every field before changing a
         * project slot. A failed commit leaves the current project intact. */
        if (nf_import_project(link->state, slot, link->staging,
                              sizeof link->staging)) return -3;
        link->uploading = 0u;
        link->received = 0u;
        return 0;
    case NF_CMD_PROJECT_IMPORT_ABORT:
        if (request->length != 1u || !link->uploading ||
            slot != link->upload_slot) return -1;
        link->uploading = 0u;
        link->received = 0u;
        return 0;
    default:
        return -2;
    }
}

static int reply_to(nf_link *link, const nf_packet *request,
                    uint8_t *out, size_t capacity)
{
    nf_packet response;
    int rc;
    size_t n;
    response.command = (uint8_t)(request->command | 0x80u);
    response.sequence = request->sequence;
    response.length = 1u;
    response.payload[0] = 0u;
    if (request->command >= NF_CMD_PROJECT_EXPORT &&
        request->command <= NF_CMD_PROJECT_IMPORT_ABORT)
        rc = project_command(link, request, &response);
    else
        rc = nf_packet_apply(link->state, request, &response);
    if (rc) {
        response.length = 1u;
        response.payload[0] = (uint8_t)(rc == -2 ? 2u : rc == -3 ? 3u : 1u);
    }
    n = nf_packet_encode(&response, out, capacity);
    return n ? (int)n : -2;
}

int nf_link_feed(nf_link *link, uint8_t byte, uint8_t *reply, size_t capacity)
{
    nf_stream *stream;
    nf_packet request;
    int rc;
    uint16_t payload_length;
    if (!link || !link->state || !reply) return -1;
    stream = &link->stream;
    if (!stream->used && byte != (uint8_t)NF_PACKET_MAGIC) return 0;
    if (stream->used == 1u && byte != (uint8_t)(NF_PACKET_MAGIC >> 8)) {
        stream->used = 0u;
        return byte == (uint8_t)NF_PACKET_MAGIC ?
            nf_link_feed(link, byte, reply, capacity) : -1;
    }
    stream->bytes[stream->used++] = byte;
    if (stream->used == 7u) {
        payload_length = (uint16_t)(stream->bytes[5] |
                            ((uint16_t)stream->bytes[6] << 8));
        if (stream->bytes[2] != NF_PACKET_VERSION ||
            payload_length > NF_PACKET_MAX_PAYLOAD) {
            stream->used = 0u;
            return -1;
        }
        stream->expected = (uint16_t)(payload_length + 9u);
    }
    if (!stream->expected || stream->used < stream->expected) return 0;
    rc = nf_packet_decode(&request, stream->bytes, stream->used);
    stream->used = stream->expected = 0u;
    if (rc) return -1;
    return reply_to(link, &request, reply, capacity);
}
