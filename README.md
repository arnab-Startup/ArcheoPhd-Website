# ArchaeoPhD Web Distribution & Landing Hub

This folder contains the official web landing page and installer distribution repository for **ArchaeoPhD Desktop Workstation**.

## Structure

```
web/
├── index.html               # Main landing page & interactive installer hub
├── styles.css               # Vanilla CSS modern design system (Obsidian Slate + Terracotta)
├── app.js                   # Interactive client-side engine (OS check, C-14 lab, Harris matrix)
├── README.md                # This file
└── downloads/               # Official distribution releases and scripts
    ├── ArchaeoPhD-Setup.exe # Native Windows C++ desktop client installer (9.52 MB)
    ├── ArchaeoPhD.exe       # Direct portable executable (9.52 MB)
    ├── WebView2Loader.dll   # Microsoft Edge WebView2 runtime loader
    ├── install.bat          # 1-click Windows batch installer with RAM & architecture checks
    ├── install.ps1          # Automated PowerShell deployment script
    ├── manifest.json        # Real-world release manifest with HuggingFace model endpoints
    └── checksums.sha256     # Cryptographic SHA-256 verification hashes
```

## Application Size Reality & Real-World Distribution Architecture

A common observation during deployment is the contrast in application size:

| Component | Disk Size | Location / Packaging |
| :--- | :---: | :--- |
| **Native Win32 Engine (`ArchaeoPhD.exe`)** | **9.52 MB** | Distributed via `downloads/ArchaeoPhD-Setup.exe` |
| **WebView2 Dynamic Loader** | 190 KB | Distributed via `downloads/WebView2Loader.dll` |
| **Radiocarbon Splines (IntCal20 / SHCal20)** | 110 KB | Embedded in native C++ binary |
| **Dense Vector Embeddings (`nomic-embed-text`)** | 84.1 MB | Streamed on first launch or bundled offline |
| **Reasoning Model (`Qwen2.5-7B-Instruct-Q4_K_M`)** | **4.68 GB** | Streamed on first launch or bundled offline |
| **Total Air-Gapped Footprint** | **~4.85 GB** | Stored in user-selected Data Root (`D:\ArchaeoPhD-Data`) |

### How Real-World AI Software Packages This

In production software (such as Ollama, LM Studio, Jan, or VS Code), large multi-gigabyte models are **never** forced as a monolithic initial download on the web:

1. **Smart Web Installer (Tier 1 - 9.5 MB)**:
   - Users download `ArchaeoPhD-Setup.exe` in under 3 seconds.
   - On first launch, the installer prompts the researcher for their **Research Data Root** (allowing installation to an external SSD or `D:\` drive so `C:\` is not filled).
   - Models are downloaded in the background with progress reporting, resume capability, and SHA-256 validation.
2. **Air-Gapped Fieldwork Bundle (Tier 2 - 4.85 GB)**:
   - For remote excavation trenches (e.g. desert digs in the Indus or Nile valleys) with zero internet connectivity.
   - Self-contained portable folder pre-bundled with all model weights ready to run off a USB 3.2 drive or field workstation.
