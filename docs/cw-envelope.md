# Morse en el DM-32UV

`CONFIG_CW` habilita un lector experimental de Morse por envolvente RF y una práctica local. Está desactivado por defecto. La herramienta no añade modos a los registros de canal del codeplug.

## Recepción

Selecciona un canal o VFO FM estrecho de 12.5 kHz y abre la herramienta Morse. Configura la velocidad de la otra estación entre 5 y 40 WPM. La tarea del radio mide el RSSI del FD6818 cada 10 ms y usa un umbral de −105 dBm con 6 dB de histéresis. Lee el texto que aparece mientras se conmuta la portadora. Umbrales, cobertura de RF, selectividad y velocidad efectiva en radio quedan pendientes de medición física.

La herramienta puede recuperar caracteres de una portadora pulsada A1A a partir de cambios de energía. También puede responder a otras señales pulsadas cuyos tiempos coincidan con Morse. El texto permanece visible hasta que inicies otra lectura. Detener la lectura devuelve el radio a su ciclo habitual de ahorro. La primera pulsación PTT durante la herramienta detiene la lectura o práctica sin transmitir.

Monitor Scan puede buscar el mismo patrón en su ventana de verificación. Se requieren al menos dos caracteres consecutivos y cuatro marcas válidas. La coincidencia identifica una **envolvente compatible con Morse**. No identifica el emisor ni demuestra por sí sola que sea A1A.

## Práctica local

Puedes escuchar CQ, el nombre o indicativo configurado en el radio, 73 y texto compatible, usando el altavoz. Ajusta el tono entre 400 y 1000 Hz. Al armar la llave local, el botón Monitor controla el tono y una pulsación continua queda limitada a dos segundos. Estos controles usan el generador PCM local. Al cerrar la práctica, PTT, VOX y APRS esperan al vaciado de los búferes de altavoz.

## Lo que se conoce del hardware

La documentación de RadioFoundry muestra la salida AF del FD6818 conectada a la entrada de audio del HR_C7000. `PHASE_OUT` expone extremos de fase del camino AF y no un flujo I/Q coherente. El camino APRS aporta audio ya demodulado. Ninguna de esas rutas establece un BFO recuperable ni permite probar filtros de recepción CW más estrechos que la ruta FM usada aquí. El procesador C-SKY podría filtrar muestras coherentes si una ruta documentada las entregara.

El ID existente de SFR/hotspot transmite un tono Morse sobre una portadora FM. Para añadir A1A se requiere verificar una ruta de portadora sin modulación, conmutación y rampas de PA, tiempo de asentamiento del PLL y espectro en los bordes de las marcas. La práctica local de este cambio no activa esa ruta.

- [RadioFoundry · hardware del DM-32UV](https://radiofoundry.org/docs/spec/hardware-dm32)
- [RadioFoundry · repetidor SFR y su identificación](https://radiofoundry.org/docs/spec/sfr)

## Validación de software

Después de preparar el árbol de RadioFoundry, ejecuta `make -C tests/host radioCoreTestCw` y `./tests/host/radioCoreTestCw`. La prueba pasa RSSI simulado a la tarea de radio a 5, 20 y 40 WPM, comprueba que una portadora constante carezca de evidencia, ejercita histéresis y muestras perdidas, y verifica salida PCM local, cancelación y ausencia de llamadas TX en los casos de prueba.
