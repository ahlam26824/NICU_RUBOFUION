export default function CircularProgress({
  value = 70,          // 0-100
  size = 220,
  strokeWidth = 14,
  color = '#B9D93A',
  trackColor = '#E3EAD1',
  centerLabel,
  centerSubLabel,
}) {
  const radius = (size - strokeWidth) / 2;
  const circumference = 2 * Math.PI * radius;
  const offset = circumference - (value / 100) * circumference;

  return (
    <div className="relative" style={{ width: size, height: size }}>
      <svg width={size} height={size} className="-rotate-90">
        <circle
          cx={size / 2}
          cy={size / 2}
          r={radius}
          fill="none"
          stroke={trackColor}
          strokeWidth={strokeWidth}
        />
        <circle
          cx={size / 2}
          cy={size / 2}
          r={radius}
          fill="none"
          stroke={color}
          strokeWidth={strokeWidth}
          strokeDasharray={circumference}
          strokeDashoffset={offset}
          strokeLinecap="round"
          style={{ transition: 'stroke-dashoffset 0.6s ease' }}
        />
      </svg>
      <div className="absolute inset-0 flex flex-col items-center justify-center">
        <span className="text-4xl font-display font-semibold text-ink">{centerLabel}</span>
        {centerSubLabel && <span className="text-sm text-muted mt-1">{centerSubLabel}</span>}
      </div>
    </div>
  );
}
