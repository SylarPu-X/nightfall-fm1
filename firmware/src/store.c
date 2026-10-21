#include "nightfall.h"
#include "nf_libc.h"

#define HEADER_SIZE 16u
#define COMMIT_OFFSET 14u

static uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffu;
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8u; bit++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
    }
    return crc;
}

static uint32_t sector(const nf_store *store, unsigned slot, unsigned bank)
{
    return store->base + (uint32_t)(slot * 2u + bank) * NF_STORE_SECTOR_SIZE;
}

static int valid(nf_store *store, unsigned slot, unsigned bank, uint32_t *generation)
{
    uint8_t header[HEADER_SIZE];
    uint32_t address = sector(store, slot, bank);
    uint16_t expected;
    if (store->io.read(store->io.context, address, header, sizeof header)) return 0;
    if (header[0] != 'N' || header[1] != 'F' ||
        header[2] != 'J' || header[3] != 'S' ||
        header[4] != 1u || header[5] != slot ||
        header[6] != (uint8_t)NF_PROJECT_BLOB_SIZE ||
        header[7] != (uint8_t)(NF_PROJECT_BLOB_SIZE >> 8) ||
        header[COMMIT_OFFSET] != 0u) return 0;
    expected = (uint16_t)(header[12] | ((uint16_t)header[13] << 8));
    if (store->io.read(store->io.context, address + HEADER_SIZE,
                       store->scratch, NF_PROJECT_BLOB_SIZE)) return 0;
    if (crc16(store->scratch, NF_PROJECT_BLOB_SIZE) != expected ||
        !nf_project_blob_valid(store->scratch, NF_PROJECT_BLOB_SIZE)) return 0;
    *generation = (uint32_t)header[8] | ((uint32_t)header[9] << 8) |
                  ((uint32_t)header[10] << 16) | ((uint32_t)header[11] << 24);
    return 1;
}

void nf_store_init(nf_store *store, nf_store_io io, uint32_t base)
{
    if (!store) return;
    store->io = io;
    store->base = base;
}

int nf_store_load(nf_store *store, nf_state *state, unsigned slot)
{
    uint32_t gen[2] = {0u, 0u};
    int ok[2], first, second;
    if (!store || !state || slot >= NF_PROJECT_SLOTS || state->playing ||
        !store->io.read || store->base % NF_STORE_SECTOR_SIZE ||
        store->base > UINT32_MAX - NF_STORE_SECTORS * NF_STORE_SECTOR_SIZE)
        return -1;
    ok[0] = valid(store, slot, 0u, &gen[0]);
    ok[1] = valid(store, slot, 1u, &gen[1]);
    if (!ok[0] && !ok[1]) return -2;
    first = ok[1] && (!ok[0] || (int32_t)(gen[1] - gen[0]) > 0) ? 1 : 0;
    second = first ^ 1;
    if (ok[first] && valid(store, slot, (unsigned)first, &gen[first]) &&
        nf_import_project(state, slot, store->scratch, NF_PROJECT_BLOB_SIZE) == 0)
        return 0;
    if (ok[second] && valid(store, slot, (unsigned)second, &gen[second]) &&
        nf_import_project(state, slot, store->scratch, NF_PROJECT_BLOB_SIZE) == 0)
        return 0;
    return -2;
}

int nf_store_save(nf_store *store, const nf_state *state, unsigned slot)
{
    uint8_t header[HEADER_SIZE], commit = 0u;
    uint32_t gen[2] = {0u, 0u}, next_gen, address, offset;
    unsigned old_bank, new_bank;
    int ok[2];
    uint16_t crc;
    size_t length;
    if (!store || !state || slot >= NF_PROJECT_SLOTS || state->playing ||
        !store->io.read || !store->io.erase || !store->io.program ||
        store->base % NF_STORE_SECTOR_SIZE ||
        store->base > UINT32_MAX - NF_STORE_SECTORS * NF_STORE_SECTOR_SIZE)
        return -1;
    ok[0] = valid(store, slot, 0u, &gen[0]);
    ok[1] = valid(store, slot, 1u, &gen[1]);
    old_bank = ok[1] && (!ok[0] || (int32_t)(gen[1] - gen[0]) > 0) ? 1u : 0u;
    new_bank = (ok[0] || ok[1]) ? old_bank ^ 1u : 0u;
    next_gen = (ok[0] || ok[1]) ? gen[old_bank] + 1u : 1u;
    length = nf_export_project(state, slot, store->scratch, sizeof store->scratch);
    if (length != NF_PROJECT_BLOB_SIZE) return -2;
    crc = crc16(store->scratch, length);
    memset(header, 0xff, sizeof header);
    header[0] = 'N'; header[1] = 'F'; header[2] = 'J'; header[3] = 'S';
    header[4] = 1u; header[5] = (uint8_t)slot;
    header[6] = (uint8_t)length; header[7] = (uint8_t)(length >> 8);
    header[8] = (uint8_t)next_gen;
    header[9] = (uint8_t)(next_gen >> 8);
    header[10] = (uint8_t)(next_gen >> 16);
    header[11] = (uint8_t)(next_gen >> 24);
    header[12] = (uint8_t)crc; header[13] = (uint8_t)(crc >> 8);
    address = sector(store, slot, new_bank);
    if (store->io.erase(store->io.context, address) ||
        store->io.program(store->io.context, address, header, sizeof header)) return -3;
    for (offset = 0u; offset < length;) {
        uint32_t page_offset = (HEADER_SIZE + offset) & 255u;
        uint32_t n = 256u - page_offset;
        if (n > length - offset) n = (uint32_t)(length - offset);
        if (store->io.program(store->io.context, address + HEADER_SIZE + offset,
                              store->scratch + offset, n)) return -3;
        offset += n;
    }
    /* Read back before the one-way commit byte. A power cut anywhere before
     * this point keeps the previously committed bank untouched. */
    if (store->io.read(store->io.context, address + HEADER_SIZE,
                       store->scratch, length) ||
        crc16(store->scratch, length) != crc) return -4;
    if (store->io.program(store->io.context, address + COMMIT_OFFSET, &commit, 1u)) return -3;
    return valid(store, slot, new_bank, &next_gen) ? 0 : -4;
}
