# Single Channel Flow

Back to the [root README](../README.md).

This document is the compact signal-path reference used while iterating controls and runtime behavior.

```mermaid
flowchart TD
    subgraph InputIngress[Input Ingress]
        direction TB
        P1L[Primary Pair #1 Left]
        SFXL[SFX Pair Left]
    end

    subgraph PrimaryLane[Primary Lane]
        direction TB
        P1DEINT[Primary ingress deinterleave<br/>24-bit audio in 32-bit slots<br/>normalize to float]
        P1CLIP_NORM[Clip detect/ Metering]
        P1IN[2 or 4 channel mode]
        P1GAIN[Primary gain / trim]
        P1CLIP_GAIN[Clip detect/ Metering]
        P1EQ[Primary EQ]
        P1DUCK[Primary duck action / gain reduction]
    end

    subgraph SFXLane[SFX Lane]
        direction TB
        SFXDEINT[SFX ingress deinterleave<br/>24-bit audio in 32-bit slots<br/>normalize to float]
        SFXCLIP_NORM[Clip detect/ Metering]
        SFXIN[SFX Mono/Stereo Mode]
        SFXGAIN[SFX gain / trim]
        SFXCLIP_GAIN[Clip detect/ Metering]
        SFXEQ[SFX EQ]
        SFXDUCK_DET[SFX duck detect and metering]
    end

    P1L --> P1DEINT --> P1CLIP_NORM --> P1IN --> P1GAIN --> P1CLIP_GAIN --> P1EQ --> P1DUCK --> SUM[Primary and SFX Sum]
    SFXL --> SFXDEINT --> SFXCLIP_NORM --> SFXIN --> SFXGAIN --> SFXCLIP_GAIN --> SFXEQ --> SUM
    SFXEQ --> SFXDUCK_DET
    SFXDUCK_DET -. duck control .-> P1DUCK

    SUM --> SUM_CLIP[Clip detect/ Metering]
    SUM_CLIP --> BASS_RESTORE[Bass restoration]
    BASS_RESTORE --> ROOM_MODE[Room Mode Select]
    ROOM_MODE --> ROOM_COMP[Room Compensation]
    ROOM_COMP --> PRE_XOVER_CLIP[Clip detect/ Metering]

    PRE_XOVER_CLIP --> X1[Stage-1 2-way crossover<br/>0-65535 Hz setpoint<br/>high-pass / low-pass split]
    X1 -->|HP| X2[Stage-2 2-way crossover<br/>0-65535 Hz setpoint<br/>tweeter / midrange split]
    X1 -->|LP| LP_SUM[Low-pass lane sum]
    LP_SUM --> X3[Stage-3 2-way crossover<br/>0-65535 Hz setpoint<br/>subwoofer / infrasonic split]

    X2 -->|HP| T_CLIP[Clip detect/ Metering]
    X2 -->|LP| M_CLIP[Clip detect/ Metering]
    X3 -->|HP| W_CLIP[Clip detect/ Metering]
    X3 -->|LP| I_CLIP[Clip detect/ Metering]

    T_CLIP --> TWEETER[Tweeter lane]
    M_CLIP --> MIDRANGE[Midrange lane]
    W_CLIP --> WOOFER[Woofer lane]
    I_CLIP --> INFRA[Infrasonic lane]

    TWEETER --> T_DCBLK[Tweeter DC block]
    T_DCBLK --> T_PACK[Tweeter clamp and pack]
    T_PACK --> OUT_T[Tweeter output]

    MIDRANGE --> M_DCBLK[Midrange DC block]
    M_DCBLK --> M_PACK[Midrange clamp and pack]
    M_PACK --> OUT_M[Midrange output]

    WOOFER --> W_DCBLK[Woofer DC block]
    W_DCBLK --> W_PACK[Woofer clamp and pack]
    W_PACK --> OUT_W[Woofer output]

    INFRA --> I_DCBLK[Infrasonic DC block]
    I_DCBLK --> I_PACK[Infrasonic clamp and pack]
    I_PACK --> OUT_I[Infrasonic output]

    style P1DEINT fill:#e7f4ea,stroke:#2f6f3e,stroke-width:1px
    style P1IN fill:#e7f4ea,stroke:#2f6f3e,stroke-width:1px
    style P1GAIN fill:#e7f4ea,stroke:#2f6f3e,stroke-width:1px
    style P1EQ fill:#e7f4ea,stroke:#2f6f3e,stroke-width:1px
    style P1DUCK fill:#e7f4ea,stroke:#2f6f3e,stroke-width:1px

    style SFXDEINT fill:#fdeaea,stroke:#9a3d3d,stroke-width:1px
    style SFXIN fill:#fdeaea,stroke:#9a3d3d,stroke-width:1px
    style SFXGAIN fill:#fdeaea,stroke:#9a3d3d,stroke-width:1px
    style SFXEQ fill:#fdeaea,stroke:#9a3d3d,stroke-width:1px
    style SFXDUCK_DET fill:#fdeaea,stroke:#9a3d3d,stroke-width:1px

    style SUM fill:#fff8dc,stroke:#b08a00,stroke-width:2px
    style ROOM_MODE fill:#eef0ff,stroke:#5b63b8,stroke-width:1px
    style ROOM_COMP fill:#eef0ff,stroke:#5b63b8,stroke-width:1px
    style X1 fill:#f0f4ff,stroke:#4867c7,stroke-width:1px
    style X2 fill:#f0f4ff,stroke:#4867c7,stroke-width:1px
    style X3 fill:#f0f4ff,stroke:#4867c7,stroke-width:1px
    style LP_SUM fill:#fff8dc,stroke:#b08a00,stroke-width:1px
```

## Notes

- This page is flow-oriented and intentionally concise.
- Bypass-specific signal variants are documented in [Setup and commissioning guide](Setup_Commissioning_Guide.md#bypass-locations-in-signal-flow).
- Register-level controls for each stage are documented in [Register Manual](Register_Manual.md).
