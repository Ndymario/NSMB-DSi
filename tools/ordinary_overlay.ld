ENTRY(OrdinaryOverlay_Entry)

SECTIONS
{
    . = 0x02800020;

    .text : ALIGN(4)
    {
        *(.text*)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
    }

    .data : ALIGN(4)
    {
        *(.data*)
    }

    .bss (NOLOAD) : ALIGN(4)
    {
        __ordinary_overlay_bss_start = .;
        *(.bss*)
        *(COMMON)
        __ordinary_overlay_bss_end = .;
    }
}

