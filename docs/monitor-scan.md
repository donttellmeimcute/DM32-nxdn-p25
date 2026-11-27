# Monitor Scan en DM-32UV

Utiliza el sintonizador FD6818 y su lectura RSSI (0x67) en la frecuencia actual. El barrido sigue un raster inclusivo, con un salto directo entre las bandas normales VHF y UHF del equipo. El umbral es intensidad de señal, en dBm. Una lectura fuerte puede venir de un repetidor lejano o de un canal adyacente. El equipo no estima distancia ni detecta instantáneamente la frecuencia de una transmisión.

Cada parada espera al menos 25 ms tras sintonizar. Mantiene la escucha inicial hasta 75 ms para incluir actividad por intervalos, como una ranura de DMR. Solo abre los decodificadores ante una lectura RSSI que pasa el umbral. Los prueba en serie, porque el DM32 tiene un receptor y una ruta de demodulación digital. Una señal corta puede terminar antes de que llegue el turno del protocolo correcto.

Los módulos incluidos en el binario determinan cuáles se prueban. DMR exige una llamada con datos de enlace válidos. P25 usa la apertura del receptor P25 a partir de unidades válidas. NXDN prueba ambas velocidades y espera un encabezado o datos válidos de una llamada. YSF prueba las dos desviaciones y exige la apertura del decodificador a partir de FICH válido. M17 y D-STAR esperan el origen validado de una llamada. El resultado queda sintonizado para oír el audio que el decodificador admita y muestra los campos que se hayan recibido. Un canal cifrado puede identificarse sin entregar voz.

Tras descartar los decodificadores digitales aplicables, una señal sostenida con ruido bajo en el discriminador se presenta como candidata FM. Esa condición también puede ocurrir con una portadora sin modular. En búsqueda combinada, una señal sin clasificación y todavía fuerte puede quedar como desconocida. Si CW está habilitado, su detector busca temporización Morse en el nivel RSSI a la velocidad configurada. Es una detección de envolvente experimental, sin BFO.

La tarea del radio toma el control exclusivo del receptor antes de sintonizar. Durante la búsqueda, la pausa y la escucha de un resultado, bloquea PTT, solicitudes de host, tonos transmitidos, datos automáticos, APRS, roaming, trunking y hotspot. El DM32 también bloquea la preparación de TX y el manejo del PA antes de una ráfaga. No toma el receptor mientras exista una transmisión armada, otro escaneo, un hotspot o una sesión de trunking. Conserva la protección si el equipo rechaza la restauración. Al cerrar, vuelve al canal o VFO anterior, incluidas su frecuencia de TX y su anchura.

## Evidencia y banco

- RadioFoundry documenta las lecturas RSSI, la sintonía y la confirmación de PLL del FD6818 en [hardware-dm32](https://radiofoundry.org/docs/spec/hardware-dm32). La [hoja de datos disponible](https://www.glaver.org/blog/UV-17/FD6818.pdf) diferencia RSSI de la configuración de frecuencia.
- El simulador comprueba el raster, la obtención del resultado DMR desde un LC válido, el bloqueo de TX y la restauración. La cobertura física requiere señales conocidas, niveles controlados y medición del tiempo de adquisición en cada revisión de hardware.
