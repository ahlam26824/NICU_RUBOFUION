import { Heart, Wind, Thermometer } from 'lucide-react';
import { Link } from 'react-router-dom';
import { STATUS, formatAge } from '../lib/vitalStatus';

// Default is 'unmonitored', not 'stable'. If a caller forgets to pass a
// status, the card should look uncertain rather than falsely reassuring.
export default function BabyCard({ baby, latestVital, status = STATUS.UNMONITORED }) {
  const dotClass = {
    [STATUS.STABLE]: 'bg-good',
    [STATUS.ABNORMAL]: 'bg-alert animate-pulse',
    [STATUS.UNMONITORED]: 'bg-sage-300',
  }[status];

  const unmonitored = status === STATUS.UNMONITORED;

  return (
    <Link
      to={`/baby/${baby.id}`}
      className="card flex items-center justify-between gap-4 hover:shadow-lg transition-shadow"
    >
      <div>
        <p className="font-display font-semibold text-ink">{baby.display_name}</p>
        <p className="text-xs text-muted mt-0.5">
          Bed {baby.bed_number || '—'} · {baby.gestational_age_weeks || '—'}w
        </p>

        {/* Values are dimmed when we know they are not current, so a stale
            number is not mistaken for a live one. */}
        <div className={`flex gap-3 mt-3 ${unmonitored ? 'opacity-40' : ''}`}>
          <span className="flex items-center gap-1 text-sm text-ink">
            <Heart size={14} className="text-alert" />
            {latestVital?.heart_rate ?? '--'}
          </span>
          <span className="flex items-center gap-1 text-sm text-ink">
            <Wind size={14} className="text-good" />
            {latestVital?.spo2 ?? '--'}%
          </span>
          <span className="flex items-center gap-1 text-sm text-ink">
            <Thermometer size={14} className="text-warn" />
            {latestVital?.temperature ?? '--'}°
          </span>
        </div>

        {unmonitored && (
          <p className="text-xs text-warn font-medium mt-2">
            {latestVital
              ? `No signal · last reading ${formatAge(latestVital.recorded_at)}`
              : 'No signal · never reported'}
          </p>
        )}

        {/* Labelled on the card itself, not only in the page banner — this is
            where someone actually reads the numbers. */}
        {latestVital?.is_demo && (
          <p className="text-xs text-muted mt-2">
            <span className="font-semibold text-warn">Demo</span> · simulated,
            no device reporting
          </p>
        )}
      </div>

      <span className={`w-3 h-3 rounded-full flex-shrink-0 ${dotClass}`} />
    </Link>
  );
}
