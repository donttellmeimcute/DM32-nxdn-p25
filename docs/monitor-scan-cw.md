# Monitor Scan y Morse en el DM-32UV

## Qué permite la placa

En las dos configuraciones RF del DM-32UV, la ruta de barrido usa la
sintonía del FD6818 y lee RSSI del registro 0x67 después de cada cambio.
No se ha identificado un contador de frecuencia de banda ancha en esa ruta.
Los intervalos quedan sujetos al enganche del PLL, la cuadrícula elegida y
el tiempo necesario para recibir tramas válidas de cada modo. Un pico de
RSSI identifica una señal candidata. El modo y sus metadatos requieren
evidencia del receptor en esa frecuencia.

El FD6818 entrega audio demodulado al altavoz por la ruta FM o al HR_C7000
por la ruta digital. La lectura `PHASE_OUT` del HR_C7000 informa extremos
de fase de la vía AF. El código de la placa carece de una salida de I/Q
coherente o de un BFO comprobado para recuperar el batido de una portadora
CW. Los valores conocidos de unos registros del FD6818 coinciden con
el BK4819. El resto, incluido su posible modo SSB, requiere evidencia
independiente.

Morse experimental usa las transiciones de la envolvente RSSI para reconocer
marcas y espacios. Su selectividad parte del filtro de RF probado para FM
estrecha. Un decodificador de envolvente tiene dificultades con interferencia,
desvanecimiento y estaciones simultáneas dentro del filtro. La preescucha y
la práctica Morse usan el altavoz. El ID Morse de SFR ya existente emite
un tono modulado en FM. El código de SFR no establece una ruta de
portadora A1A. El control del PLL y del PA para A1A requiere verificar un
portador sin modulación y medir la forma de sus flancos y sus emisiones
adyacentes en una carga de 50 Ω.

- [Hardware DM-32UV de RadioFoundry](https://radiofoundry.org/docs/spec/hardware-dm32)
- [HR_C7000, referencia del módem fijada en la compilación](https://github.com/RadioFoundry/RadioFoundry/blob/e89121e97a314f0ddd0475f848713ba10710d7db/docs/spec/modem-hrc7000.md)
- [Identificación Morse de SFR](https://radiofoundry.org/docs/spec/sfr)

## Instalación y compilación

En **Custom Modes** del flasher elige **Monitor Scan**, **CW** o ambos.
La release incluye esas combinaciones con los modos digitales de Full,
Multimode y DMR + FM. El flasher ofrece compilar localmente las demás
combinaciones. Los tres perfiles estándar dejan ambos módulos apagados.

```sh
python3 tools/build_current.py --monitor-cw-tests-only
python3 tools/build_current.py --tree build/monitor-cw --with monitor-scan --with cw
```

`MONITOR_SCAN` y `CW` son opciones independientes. Los perfiles Full,
Multimode y DMR + FM las dejan desactivadas por defecto. El manifiesto
declara qué módulos conoce la compilación y cuáles trae habilitados.
Monitor Scan y la práctica Morse usan ajustes de sesión. Los canales que se
guardan usan los tipos y el indicador RX Only del codeplug existente.

Abre **Herramientas → Escaneo cercano** o **Herramientas → Morse**. Desde el
VFO, abre **Rápido** y pulsa **#** para mostrar **Señales**. Configura banda
o límites, paso, umbral y modo antes de iniciar el barrido. Un resultado
mantiene la frecuencia para escucharla y permite continuar, omitir o
guardar. Una frecuencia sin modo identificado o con envolvente Morse
puede guardarse como referencia **FM RX Only**.

- [Procedimiento de Monitor Scan](monitor-scan.md)
- [Configuración y práctica Morse](cw-envelope.md)

## Validación con capturas

El conjunto de interoperabilidad prueba los receptores MMDVM sobre símbolos
generados por el núcleo de radio. MMDVM-IQ evalúa NXDN96 a un reloj de
2400 símbolos/s para comprobar el contenido de las tramas. Esa prueba
de protocolo no mide adquisición NXDN96 a su reloj de RF nativo.

```sh
python3 tools/test_mmdvm_interop.py --tree build/monitor-cw
python3 tools/replay_mmdvm_iq.py dstar captura_dstar.npz --invert
python3 tools/replay_mmdvm_iq.py ysf 'captura_dstar(1).npz' --invert
```

El segundo archivo contiene YSF DN según su FICH. En las capturas de
referencia a 435 MHz, MMDVM-IQ recuperó una cabecera D-STAR y 179 tramas
de voz. MMDVMHost verificó el CRC del FICH en las 39 tramas YSF. El
reproductor usa los campos `iq`, `rate` y `offset` de los archivos NPZ,
aplica un filtro de canal, pasa por el discriminador a 24 kHz y usa las
tablas de recepción de MMDVM-IQ. Requiere NumPy y SciPy además de las
dependencias del conjunto MMDVM. Las capturas proceden de compilaciones
anteriores y sirven como referencia de decodificación.

Para comprobar Monitor Scan en la placa, usa un generador o un segundo
transceptor de banco con carga y atenuación. Repite sobre una retícula
con un canal activo y varios vacíos. Mide tiempo hasta sintonía, tiempo
hasta decodificación y falsos positivos para varias intensidades. Durante
pausa y escucha, comprueba que PTT y los productores automáticos de TX
permanezcan inhibidos. Al salir compara frecuencia, modo y estado de la
pantalla con los valores anteriores. Para Morse por envolvente, usa
una portadora pulsada a distintas velocidades y repite con una señal
adyacente. Estas mediciones no se han ejecutado en un DM32 físico.
