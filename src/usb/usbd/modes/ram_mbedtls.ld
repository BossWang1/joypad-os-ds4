/* ram_mbedtls.ld - place mbedTLS code in SRAM (loaded from flash at boot).
 *
 * Linked as an *additional* script via `target_link_options -T <this>` so it
 * is merged into the pico-sdk default `memmap_default.ld` via INSERT BEFORE.
 *
 * Why:  Core 1 runs mbedTLS RSA-PSS signing (~3.4s on RP2040) while Core 0
 *       continues polling USB host, app glue, etc.  If mbedTLS lives in flash
 *       (XIP), every Core 0 task that touches flash thrashes the 16 KB shared
 *       XIP cache and stalls Core 1's instruction fetch — observed to hang
 *       the sign indefinitely.  Pinning mbedTLS to SRAM eliminates this.
 *
 * Coverage:
 *   - libmbedcrypto.a (rsa.c, bignum.c, sha256.c, pkcs1.c, md.c, etc.)
 *   - libmbedx509.a / libmbedtls.a (defensive — unused by PSS sign but cheap)
 *   - any function-section *.text.mbedtls_*  / *.text.mpi_*  that escape
 *     archive-name matching (object-library builds, LTO, etc.)
 *
 * Cost: ~30–60 KB SRAM permanently reserved (RP2040 has 264 KB total).
 */

SECTIONS
{
    .time_critical.mbedtls : ALIGN(4)
    {
        __mbedtls_ram_start__ = .;
        *libmbedcrypto.a:(.text .text.*)
        *libmbedx509.a:(.text .text.*)
        *libmbedtls.a:(.text .text.*)
        *(.text.mbedtls_*)
        *(.text.mpi_*)
        . = ALIGN(4);
        __mbedtls_ram_end__ = .;
    } > RAM AT> FLASH
} INSERT BEFORE .text;
