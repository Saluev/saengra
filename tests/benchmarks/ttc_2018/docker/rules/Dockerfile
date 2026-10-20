# Rule engines for the TTC 2018 comparison: Drools (JVM) and CLIPS (via clipspy), with rules
# written for this benchmark (see drools/ and ttc_2018/clips_engine.py), not contest solutions.
# Built and run automatically by tests/benchmarks/test_ttc_2018.py.
FROM maven:3.9.9-eclipse-temurin-21

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir clipspy==1.0.6

COPY drools/pom.xml /build/drools/pom.xml
RUN cd /build/drools && mvn -q -B dependency:go-offline
COPY drools/src /build/drools/src
RUN cd /build/drools && mvn -q -B package -DskipTests \
    && cp target/ttc2018-drools-1.0-jar-with-dependencies.jar /opt/drools-ttc.jar

ENV PATH=/opt/venv/bin:$PATH \
    PYTHONDONTWRITEBYTECODE=1
