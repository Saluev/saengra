package waltz;

/** Fact types of the Waltz benchmark, mirroring the deftemplates of waltz.clp. */
public final class Facts {

    private Facts() {
    }

    public static class Stage {
        private String value;

        public Stage(String value) {
            this.value = value;
        }

        public String getValue() {
            return value;
        }

        public void setValue(String value) {
            this.value = value;
        }
    }

    public static class Line {
        private final int p1;
        private final int p2;

        public Line(int p1, int p2) {
            this.p1 = p1;
            this.p2 = p2;
        }

        public int getP1() {
            return p1;
        }

        public int getP2() {
            return p2;
        }
    }

    public static class Edge {
        private final int p1;
        private final int p2;
        private boolean joined;
        private String label = "nil";
        private boolean plotted;

        public Edge(int p1, int p2) {
            this.p1 = p1;
            this.p2 = p2;
        }

        public int getP1() {
            return p1;
        }

        public int getP2() {
            return p2;
        }

        public boolean isJoined() {
            return joined;
        }

        public void setJoined(boolean joined) {
            this.joined = joined;
        }

        public String getLabel() {
            return label;
        }

        public void setLabel(String label) {
            this.label = label;
        }

        public boolean isPlotted() {
            return plotted;
        }

        public void setPlotted(boolean plotted) {
            this.plotted = plotted;
        }
    }

    public static class Junction {
        private final int p1;
        private final int p2;
        private final int p3;
        private final int basePoint;
        private final String type;

        public Junction(int p1, int p2, int p3, int basePoint, String type) {
            this.p1 = p1;
            this.p2 = p2;
            this.p3 = p3;
            this.basePoint = basePoint;
            this.type = type;
        }

        public int getP1() {
            return p1;
        }

        public int getP2() {
            return p2;
        }

        public int getP3() {
            return p3;
        }

        public int getBasePoint() {
            return basePoint;
        }

        public String getType() {
            return type;
        }
    }

    private static final int MOD = 100;

    private static double angle(int p1, int p2) {
        return Math.atan2(p2 % MOD - p1 % MOD, p2 / MOD - p1 / MOD);
    }

    private static double inscribedAngle(int base, int p1, int p2) {
        double diff = Math.abs(angle(base, p1) - angle(base, p2));
        return diff > Math.PI ? 2 * Math.PI - diff : diff;
    }

    /** Port of make-3-junction from waltz.clp (and of classify_3_junction from workload.py). */
    public static Junction make3Junction(int base, int p1, int p2, int p3) {
        double a12 = inscribedAngle(base, p1, p2);
        double a13 = inscribedAngle(base, p1, p3);
        double a23 = inscribedAngle(base, p2, p3);
        double s1213 = a12 + a13;
        double s1223 = a12 + a23;
        double s1323 = a13 + a23;
        double sum;
        int shaft;
        int barb1;
        int barb2;
        if (s1213 < s1223) {
            if (s1213 < s1323) {
                sum = s1213; shaft = p1; barb1 = p2; barb2 = p3;
            } else {
                sum = s1323; shaft = p3; barb1 = p1; barb2 = p2;
            }
        } else {
            if (s1223 < s1323) {
                sum = s1223; shaft = p2; barb1 = p1; barb2 = p3;
            } else {
                sum = s1323; shaft = p3; barb1 = p1; barb2 = p2;
            }
        }
        String type;
        if (Math.abs(sum - Math.PI) < 0.001) {
            type = "tee";
        } else if (sum > Math.PI) {
            type = "fork";
        } else {
            type = "arrow";
        }
        return new Junction(barb1, shaft, barb2, base, type);
    }
}
