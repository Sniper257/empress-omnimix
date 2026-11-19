#ifndef MMIX_SELECT_PROTOCOL_H
#define MMIX_SELECT_PROTOCOL_H
/* Shared EE/IOP ABI. Little endian RPC; UDP payload is also explicitly LE.
 * Native IP/UDP/Ethernet headers are emitted separately in network byte order. */
#define MSP_RPC 0x4d535031u
#define MSP_ABI 1u
#define MSP_PORT 27777u
#define MSP_CHUNK 1024u
#define MSP_PACKET (MSP_CHUNK+48u)
#define MSP_SLOTS 4u
#define MSP_ROUND(n) (((n)+63u)&~63u)
enum {MSP_STATUS=0,MSP_START=1,MSP_DATA=2,MSP_STOP=3};
typedef struct {unsigned abi,token,bytes,peer,local;unsigned char data[MSP_PACKET];} msp_request;
typedef struct {unsigned abi;int result;unsigned link,dropped,clock_lo,clock_hi,gate,token,queued,free_total,free_max,ack_session,ack_crc;} msp_reply;
/* UDP envelope: magic8, schema, session, sequence, fragments, total, offset,
 * payload length, payload CRC32, complete blob CRC32, header CRC32 =48 bytes. */
#define MSP_MAGIC "MMIXUDP1"
#endif
