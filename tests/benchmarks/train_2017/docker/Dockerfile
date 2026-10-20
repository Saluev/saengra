# Third-party engines for the Train Benchmark comparison: Drools and Apache Jena (JVM), CLIPS (via clipspy).
# Built and run automatically by tests/benchmarks/test_train_2017.py.
FROM maven:3.9.9-eclipse-temurin-21

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir clipspy==1.0.6

COPY drools/pom.xml /build/drools/pom.xml
RUN cd /build/drools && mvn -q -B dependency:go-offline
COPY drools/src /build/drools/src
RUN cd /build/drools && mvn -q -B package -DskipTests \
    && cp target/train-benchmark-drools-1.0-jar-with-dependencies.jar /opt/drools-bench.jar

COPY jena/pom.xml /build/jena/pom.xml
RUN cd /build/jena && mvn -q -B dependency:go-offline
COPY jena/src /build/jena/src
RUN cd /build/jena && mvn -q -B package -DskipTests \
    && cp target/train-benchmark-jena-1.0-jar-with-dependencies.jar /opt/jena-bench.jar

ENV PATH=/opt/venv/bin:$PATH \
    PYTHONDONTWRITEBYTECODE=1
