import { NavLink } from 'react-router-dom';
import { LayoutGrid, BellRing, Stethoscope, Settings } from 'lucide-react';

const items = [
  { to: '/dashboard', icon: LayoutGrid, label: 'Dashboard' },
  { to: '/alerts', icon: BellRing, label: 'Alerts' },
  { to: '/consulting', icon: Stethoscope, label: 'Doctors' },
  { to: '/settings', icon: Settings, label: 'Settings' },
];

export default function BottomNav() {
  return (
    <nav className="fixed bottom-0 left-0 right-0 bg-card border-t border-sage-200 px-6 py-3 flex justify-between max-w-md mx-auto">
      {items.map(({ to, icon: Icon, label }) => (
        <NavLink
          key={to}
          to={to}
          className={({ isActive }) =>
            `flex flex-col items-center gap-1 text-xs ${isActive ? 'text-ink font-semibold' : 'text-muted'}`
          }
        >
          <Icon size={22} strokeWidth={2} />
          {label}
        </NavLink>
      ))}
    </nav>
  );
}
