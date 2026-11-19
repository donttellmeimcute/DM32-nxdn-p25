# DM32-nxdn-p25 — NXDN-P25-DM32 multimodo (FM · AM · DMR · P25 · NXDN) para el Baofeng DM-32UV

[![Regálame un radio en Throne](https://img.shields.io/badge/Throne-Reg%C3%A1lame%20un%20radio-ff69b4?style=for-the-badge)](https://throne.com/lyraarsgoetia) [![Apóyame en Ko-fi](https://img.shields.io/badge/Ko--fi-Ap%C3%B3yame-ff5e5b?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lyraarsgoetia)

**¿Quieres firmware para tu radio?** Regálamelo desde mi lista de deseos y lo porto: **<https://throne.com/lyraarsgoetia>** · *Want firmware for your radio? Gift it from my wishlist and I'll port it.*

Firmware abierto y multimodo para el **Baofeng DM-32UV**: FM, AM, DMR (Tier II y **Tier III troncal**), **APCO P25 fase 1**
y **NXDN48/NXDN96**, con **AES-256** en DMR, P25 y NXDN. Desde el 17 de noviembre de 2026 se compila sobre
[RadioFoundry](https://github.com/RadioFoundry/RadioFoundry) actual (commit `e89121e9`), que ya trae P25 y NXDN, más
los cambios de esta rama en [`integration/current/base.patch`](integration/current/base.patch). Deriva de
[Open25-DM32](https://github.com/tylerwatt12/Open25-DM32) (tylerwatt12). La mantiene **lyra XE1POC**.

> Firmware experimental. Úsalo bajo tu responsabilidad, solo en frecuencias en las que tengas licencia y con un respaldo de tu radio.
>
> **Sobre AES-256:** solo para sistemas comerciales / Part 90 autorizados para cifrar. No lo uses en bandas de radioaficionado: el servicio de aficionados es en claro, aun con licencia.

## Estado (18 de noviembre de 2026, main `3e83541`)

Última release al escribir esto: `v2026.11.18-8d018382` (el CI está compilando la nueva con el all-call; el flasher todavía sirve `d30230a8`).

| Función | Estado |
|---|---|
| Base | RadioFoundry `e89121e9`, instalada y arrancando en la radio del autor, más `integration/current/base.patch`. Trae la desviación de TX de P25/NXDN medida en el HR_C7000 (RadioFoundry #644) |
| NXDN96 en el aire | DSD+ sincroniza y decodifica llamadas NXDN96 de este firmware (18 nov, 13:45: TG 91, RID 321, RAN 22, tramas de voz fluyendo) |
| NXDN TX | El monitor de servicio de KN4SWB midió FSK error 19,3 % y desviación de símbolo 993 Hz (nominal 1050 Hz) en 441,1125 MHz. **All-call portado:** un canal sin talkgroup ya transmite destino all-call (`e6c105f`, `fix_dst()` en el engine NXDN, con test host). **40 Hz no portable:** el fix candidato (`d30230a`, `.flat_tx_hpf_hz = 40` en el tap de audio plano de la serie vieja) no tiene equivalente en este build — el TX NXDN va por el engine 4FSK nativo del HR_C7000 y la TX RAM lleva símbolos, no audio, así que no hay high-pass que pre-compensar (`3e83541`, documentado en `docs/spec/nxdn.md`). No se toca el gain ni la desviación. Re-medición de KN4SWB pendiente |
| Modo por canal | Fila **Modo** en cada canal: FM, AM, DMR, P25, NXDN 4800, NXDN 9600 |
| AM | Tipo de canal AM propio, solo RX, en cualquier frecuencia que sintonice el receptor (p. ej. banda aérea) |
| Recepción ampliada | Activada por defecto. Solo RX fuera de las bandas de TX |
| Monitor digital (DMon) | En P25 y NXDN: oye llamadas de cualquier grupo, NAC o RAN del canal |
| AES-256 DMR | Formato Motorola (PI header FID 0x10, ALG 0x25, KID 1-255). En **Ajustes › Privacidad › Tipo clave: AES-256**, con el ID de la clave; la clave se carga en el editor (pestaña Radio, «DMR AES-256»). El keystream descifra 180 de 180 tramas de una grabación real con clave publicada. **Sin probar contra un Motorola en el aire** |
| AES-256 P25 | Formato TIA-102.AAAD-A (ALGID `0x84`, KID de 16 bits). El keystream coincide con el de OP25 para una clave y un MI de referencia. **Sin probar contra un Motorola u otra marca** |
| AES-256 NXDN | Formato del estándar NXDN TS 1-D (cipher 3, KID 0-63, VCALL_IV). Coincide bit a bit con los 32 vectores de prueba del estándar. Entrada tardía: descifra desde la sesión siguiente (hasta 1,3 s). **Sin probar contra Icom o Kenwood** |
| DMR Tier III | Estación móvil: control channel, registro, llamadas de grupo y privadas, datos cortos (RadioFoundry). Sin probar en un sistema real |
| P25 troncal | Recepción y seguimiento de llamadas (RadioFoundry). Sin TX |
| Tonos de permiso estilo Motorola | DMR: el chirp de MOTOTRBO (cuatro notas rápidas). P25 y NXDN: el permiso de ASTRO 25 (tres bips de 910 Hz) antes de transmitir. Se apagan en Ajustes › Sonido › Tono de permiso. El primer PTT DMR tras NXDN, FM o P25 ya no sale sin voz: la voz retoma en su primer bloque la ruta del altavoz que el tono de permiso dejaba encendida (el fix 0043 se había perdido al cambiar de base; `3f1c173`, cubierto por `ambeVoiceTest` en host). **Pendiente confirmación en el aire** de ese primer PTT |
| APRS, GPS, hotspot | Los de RadioFoundry; sin probar en el aire con esta imagen |

## Cambio de base: qué se pierde y qué hacer

La versión anterior (`ce281744`) era una serie de parches sobre un RadioFoundry más viejo. Esta la reemplaza. Se
pierde, por ahora:

| Antes | Ahora |
|---|---|
| Cuatro claves AES en EEPROM (`0x7C00`) y la clave por canal en el byte `0x32` | Las claves van en el llavero del codeplug (CPS web, pestaña Radio). **Vuelve a cargar tus claves** después de actualizar y revisa los canales que tenían AES |
| P25 fase 2 (TDMA), TX troncal P25 | No están |
| AES-256 en DMR por canal (byte `0x32`) | AES-256 en DMR para toda la radio, en Ajustes › Privacidad (como la privacidad DMR de RadioFoundry) |
| Tema «Rosa», mensajes DMR de texto libre | El tema Rosa volvió como capa Kawaii (`CONFIG_UI_KAWAII`, `7c972f0`): tema rosa claro en toda la UI y foto de arranque. Los mensajes DMR de texto libre de la serie vieja siguen fuera; lo que trae esta base es DMR SMS/TMS de RadioFoundry |

En la radio del autor, actualizar desde la versión anterior conservó canales y calibración. Haz siempre el respaldo
que ofrece el flasher. La serie de parches anterior queda en `integration/radiofoundry/` y `docs/` como historia; ya no se compila.

## Hotspots, repetidores y otras radios

El NAC/RAN de **TX** debe coincidir con el del otro equipo; Pi-Star usa NAC 293 y RAN 1. NXDN 4800 es el «VN» de Icom
(6,25 kHz). Si el hotspot necesita un offset de cientos de Hz, corrige la radio en **Ajustes › Avanzado › Ajuste frec**.

## Instalar

**Seleccionar una clave AES-256 por canal.** Carga las claves en **Radio → Claves AES-256** del
CPS web. Abre un canal DMR, P25 o NXDN convencional y selecciona el ID en su campo AES-256.
También puedes elegirlo con las flechas en **Canales → Editar canal → Clave AES-256** en la DM32
y mantener **OK** para guardar. Cada modo muestra los IDs de su llave. DMR permite **Predeterminado
de la radio**, que usa la configuración general de privacidad. P25 y NXDN permiten **Desactivado**.

1. Usa el **web flasher**: <https://donttellmeimcute.github.io/dm32-web-cps-flasher/> (Chrome o Edge de escritorio, Web Serial), o descarga la imagen `.bin` sin códec de la última [release](../../releases). El flasher sincroniza cada varias horas, no al instante: si muestra una versión anterior a la última release de GitHub, espera a que corra su sincronización o baja el `.bin` de la release.
2. Pon la radio en modo actualización: apagada, mantén **PTT + SK1** al encenderla (LED verde).
3. El flasher hace un respaldo, toma el códec AMBE de tu propia radio (o del archivo de firmware de fábrica que elijas) y lo instala junto al firmware.

**Sobre el AMBE:** el códec AMBE+2 (voz DMR y NXDN) es de DVSI y Baofeng. **No se distribuye aquí**: ni la release ni el repositorio lo contienen. Solo pasa entre tu radio, tus archivos y tu navegador. Sin él funcionan FM, AM y P25 (IMBE libre); DMR y NXDN necesitan el códec de tu radio.

Si necesitas el archivo original, Baofeng ofrece el [firmware oficial DM-32UV v1.01.048 (ZIP)](https://baofeng.s3.amazonaws.com/Baofeng_DM-32UV_Firmware_v1.01.048.zip), enlazado desde su [página de descargas](https://www.baofengradio.com/pages/download). El flasher reconoce ese ZIP en el selector de firmware original. No hay una descarga independiente de AMBE alojada en este proyecto. La [licencia pública de DVSI, sección 2.3](https://www.dvsinc.com/papers/DVSI%20End%20User%20Product%20Liceanse%20Agreement.pdf), exige consentimiento escrito para distribuir su software a terceros; no contamos con ese permiso.

## Compilar

Linux (o WSL) con el toolchain C-SKY (`tools/bootstrap_toolchain.sh` y `tools/bootstrap_newlib.sh`, ver [docs/build.md](docs/build.md)):

```sh
python3 tools/build_current.py      # build/current: RadioFoundry e89121e9 + integration/current/base.patch
make -C build/current/tests/host voiceCipherTest nxdnEngineTestAes p25EngineTestAes
```

La imagen queda en `build/current/build/dm32/rfoundry_dm32.bin`. Detalle del cifrado: `docs/spec/air-encryption.md` dentro del árbol `build/current`.

**Compilación y release automáticas:** cada commit a `main` hace que la acción `Firmware` (`.github/workflows/firmware.yml`) compile la imagen sin códec, pase las pruebas de cifrado, P25 y NXDN, y publique una release `v<AAAA.MM.DD>-<commit>` con `rfoundry-dm32-<versión>.bin`, su manifiesto `.json` y `SHA256SUMS`. El web flasher la instala solo en unos 15 minutos. Nunca descarga el firmware de fábrica ni el AMBE.

## Apoya el proyecto

¿Quieres soporte para otro radio? Si me regalas uno desde mi lista de deseos, puedo hacer firmware para él:
**<https://throne.com/lyraarsgoetia>**

También puedes apoyar el desarrollo del firmware y del flasher en Ko-fi: **<https://ko-fi.com/lyraarsgoetia>**

Apoyar en Ko-fi o regalar un radio es apoyar mi tiempo de desarrollo. El firmware es gratuito y libre (GPL-3.0-or-later): apoyar no compra el firmware, ni acceso anticipado, ni el códec AMBE. El AMBE no se distribuye aquí y sigue saliendo de tu propia radio.

## Créditos y licencia

- **Open25-DM32** original: tylerwatt12 (KD8ZMM). Puedes apoyar su trabajo en [GitHub Sponsors](https://github.com/sponsors/tylerwatt12?metadata_project=open25-dm32) o [Ko-fi](https://ko-fi.com/kd8zmm).
- **RadioFoundry**: The RadioFoundry Authors (MIT; avisos conservados en el parche).
- Fila de modo con AM, recepción ampliada, DMon en P25/NXDN, AES-256 en DMR/P25/NXDN, tonos de permiso estilo Motorola y la
  serie anterior: **lyra XE1POC**.

El proyecto se distribuye bajo [GPL-3.0-or-later](LICENSE) salvo que un archivo indique otra cosa (el código de RadioFoundry es MIT). Procedencia de cada fuente: [docs/licenses.md](docs/licenses.md).

---

## English summary

Multi-mode open firmware for the Baofeng DM-32UV: FM, AM, DMR (Tier II and Tier III trunking), P25 Phase 1 and
NXDN48/NXDN96, with AES-256 voice on DMR (Motorola layout, decrypts a real recording with a published key), P25
(TIA-102.AAAD-A layout, matches OP25's keystream) and NXDN (matches the
NXDN TS 1-D test vectors). Since 2026-11-17 it builds on current [RadioFoundry](https://github.com/RadioFoundry/RadioFoundry)
(`e89121e9`) plus `integration/current/base.patch`; the old patch series no longer builds. Motorola-style talk permit tones. The first DMR PTT after NXDN/FM/P25 no longer goes out voiceless (`3f1c173`, host-tested; on-air confirmation still pending). DSD+ decodes NXDN96 calls from this firmware (18 Nov: TG 91, RID 321, RAN 22). A talkgroup-less NXDN channel now transmits all-call (`e6c105f`). The 40 Hz NXDN TX pre-compensation (`d30230a`) has no equivalent in this build — NXDN TX goes through the HR_C7000 native 4FSK slot engine, not an audio path — so it was documented as not portable (`3e83541`), not ported. Lost
in the switch: P25 Phase 2, P25 trunking TX; AES keys must be loaded again. The pink theme is back as the Kawaii layer (`7c972f0`). Releases ship the image **without** the proprietary AMBE
codec; the [web flasher](https://donttellmeimcute.github.io/dm32-web-cps-flasher/) takes it from your own radio.
Interoperability with other makers' encrypted radios is untested. GPL-3.0-or-later, RadioFoundry code MIT.

**Support:** want firmware for another radio? Gift one from my wishlist and I can port it:
<https://throne.com/lyraarsgoetia> · Support development on Ko-fi: <https://ko-fi.com/lyraarsgoetia>
Donations support development time. The firmware is free (GPL-3.0-or-later): supporting does not buy the firmware, early access, or the AMBE codec, which is not distributed here and still comes from your own radio.
