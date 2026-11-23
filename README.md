# DYM Cyber – Prototipo de monitoreo IoT

Proyecto académico orientado al diseño de un prototipo para el monitoreo de velocidad de una banda transportadora utilizada en el proceso de chancado.

## Objetivo

Simular la medición de velocidad, calcular desviaciones respecto a una velocidad nominal y generar alertas cuando los valores superen el umbral permitido.

## Tecnologías utilizadas

- Arduino Uno R3.
- Tinkercad Circuits.
- Programación en C++ para Arduino.
- GitHub para control de versiones.
- GitHub Actions para integración continua.
- Arduino CLI para compilación automática.

## Funcionalidades del prototipo

- Inicio y detención mediante pulsador.
- Control de motor DC.
- Simulación de velocidad mediante potenciómetro.
- Indicadores LED de funcionamiento.
- Alertas de subvelocidad y sobrevelocidad.
- Visualización de mediciones en Monitor Serial.
- Identificación de sesiones y mediciones.
- Configuración de velocidad nominal y umbral.

## Pipeline DevOps

El repositorio incluye un workflow de GitHub Actions que permite comprobar automáticamente la compilación del firmware para Arduino Uno R3.

Las pruebas funcionales del circuito se realizan de manera independiente en Tinkercad.

## Arquitectura futura

Se propone integrar comunicación MQTT, almacenamiento Cloud y un dashboard para consulta de mediciones, alertas y registros históricos. Estas funcionalidades no están implementadas en el firmware actual.

## EV3 · Kairós (Wokwi + ESP32 + MQTT + Flask)

La evolución del prototipo para la EV3 está en la carpeta [`ev3/`](ev3/):

- `ev3/wokwi/`: firmware ESP32 y circuito de Wokwi ([proyecto](https://wokwi.com/projects/481404802191682561)).
- `ev3/backend/`: backend Flask con flujo ETL (MQTT → SQLite) y API REST.
- `ev3/dashboard/`: dashboard web en vivo conectado al broker MQTT.
- `.github/workflows/ev3-ci.yml`: pipeline que compila el firmware, prueba el backend y valida el diagrama.
