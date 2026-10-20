# Official TTC 2018 NMF solution (run with the `incremental` argument, as NMF-Incremental does),
# built from source natively for the host architecture.
# Build context: the `solutions` directory of https://github.com/TransformationToolContest/ttc2018liveContest
#
# .NET Core 3.1 is EOL, but its SDK and runtime images are still on MCR for both amd64 and arm64,
# so the solution is built unchanged for netcoreapp3.1 (NuGet NMF-* 2.0.169) and run on the 3.1
# runtime. The prebuilt binaries checked into NMF/bin are discarded. The only difference from the
# official setup is the output location: `dotnet publish -c Release -o /opt/nmf`.
#
# Run: dotnet /opt/nmf/NMFSolution.dll incremental
FROM mcr.microsoft.com/dotnet/sdk:3.1 AS build
ENV DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 DOTNET_NOLOGO=1
COPY NMF /build/NMF
RUN cd /build/NMF \
    && rm -rf bin obj \
    && dotnet publish NMF.csproj -c Release -o /opt/nmf -p:GeneratePackageOnBuild=false

FROM mcr.microsoft.com/dotnet/runtime:3.1
ENV DOTNET_CLI_TELEMETRY_OPTOUT=1
COPY --from=build /opt/nmf /opt/nmf
