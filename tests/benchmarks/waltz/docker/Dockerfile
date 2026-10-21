# Rule engines for the Waltz benchmark: Drools (JVM) and CLIPS (via clipspy), running the
# benchmark's own rules (drools/ and waltz/waltz.clp).
# Built and run automatically by tests/benchmarks/test_waltz.py.
FROM maven:3.9.9-eclipse-temurin-21

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir clipspy==1.0.6

COPY drools/pom.xml /build/drools/pom.xml
RUN cd /build/drools && mvn -q -B dependency:go-offline
COPY drools/src /build/drools/src
RUN cd /build/drools && mvn -q -B package -DskipTests \
    && cp target/waltz-drools-1.0-jar-with-dependencies.jar /opt/waltz-drools.jar

ENV PATH=/opt/venv/bin:$PATH \
    PYTHONDONTWRITEBYTECODE=1
