import type { ComponentType } from 'react';
import mosaicMask from '../../../assets/ui-icons/icons/mosaic.svg?inline';
import {
  AppWindow,
  ArrowLeft,
  ArrowUpRight,
  Blocks,
  Brush,
  Check,
  CheckCircle2,
  ChevronDown,
  CircleDot,
  Copy,
  Crop,
  Eye,
  EyeOff,
  FileDown,
  FileText,
  Files,
  FolderOpen,
  Globe,
  Hand,
  ImageDown,
  ImagePlus,
  Images,
  Keyboard,
  Layers2,
  LoaderCircle,
  MessageSquarePlus,
  Minus,
  Moon,
  MoreHorizontal,
  MousePointer2,
  MousePointerClick,
  MoveVertical,
  Palette,
  PanelRightClose,
  PanelRightOpen,
  Pause,
  Pencil,
  Pin,
  Play,
  Plus,
  Power,
  RectangleHorizontal,
  Redo2,
  RefreshCw,
  RotateCcw,
  Save,
  Scaling,
  ScanLine,
  ScanSearch,
  ScanText,
  Search,
  Send,
  Settings,
  ShieldAlert,
  ShieldCheck,
  SlidersHorizontal,
  Sparkles,
  Square,
  Star,
  Sun,
  Terminal,
  Trash2,
  Undo2,
  Video,
  X,
  type LucideProps,
} from 'lucide-react';

export interface AppIconProps extends Omit<LucideProps, 'size'> {
  size?: number;
}

const DEFAULT_STROKE_WIDTH = 1.8;

function wrapLucide(Icon: ComponentType<LucideProps>) {
  return function CanonicalIcon({ size = 16, strokeWidth = DEFAULT_STROKE_WIDTH, ...rest }: AppIconProps) {
    return <Icon size={size} strokeWidth={strokeWidth} {...rest} />;
  };
}

/** Exact Lucide-spec EditHere icon from assets/ui-icons/icons/scroll.svg */
export function ScrollCaptureIcon({
  size = 16,
  strokeWidth = DEFAULT_STROKE_WIDTH,
  ...rest
}: AppIconProps) {
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...rest}
    >
      <rect x="5" y="2" width="14" height="20" rx="2" />
      <path d="M12 7v10m-3-7 3-3 3 3m-6 4 3 3 3-3" />
    </svg>
  );
}

/** Exact Lucide-spec EditHere icon from assets/ui-icons/icons/corner.svg */
export function CornerRadiusIcon({
  size = 16,
  strokeWidth = DEFAULT_STROKE_WIDTH,
  ...rest
}: AppIconProps) {
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...rest}
    >
      <path d="M5 3h8a8 8 0 0 1 8 8v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2Z" />
    </svg>
  );
}

/** Exact Lucide 1.55 file-braces icon from assets/ui-icons/icons/json.svg */
export function JsonFileIcon({
  size = 16,
  strokeWidth = DEFAULT_STROKE_WIDTH,
  ...rest
}: AppIconProps) {
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...rest}
    >
      <path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z" />
      <path d="M14 2v5a1 1 0 0 0 1 1h5" />
      <path d="M10 12a1 1 0 0 0-1 1v1a1 1 0 0 1-1 1 1 1 0 0 1 1 1v1a1 1 0 0 0 1 1" />
      <path d="M14 18a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1 1 1 0 0 1-1-1v-1a1 1 0 0 0-1-1" />
    </svg>
  );
}

// Capture, Redaction & Output (1:1 aligned with assets/ui-icons/manifest.json)
export const CaptureIcon = wrapLucide(ScanLine);
export const ShadowIcon = wrapLucide(Layers2);
export const PinIcon = wrapLucide(Pin);
export const OcrIcon = wrapLucide(ScanText);
export const CopyImageIcon = wrapLucide(Images);
export const CopyBundleIcon = wrapLucide(Files);
export const CopyTextIcon = wrapLucide(Copy);
export const SaveImageIcon = wrapLucide(ImageDown);
export const SaveProjectIcon = wrapLucide(Save);
export const ExportFileIcon = wrapLucide(FileDown);
export const ImportImageIcon = wrapLucide(ImagePlus);
export const OpenFolderIcon = wrapLucide(FolderOpen);

// Annotation & Canvas Tools (1:1 aligned with assets/ui-icons/manifest.json)
export const SelectToolIcon = wrapLucide(MousePointer2);
export const PanToolIcon = wrapLucide(Hand);
export const SmartSelectIcon = wrapLucide(ScanSearch);
export const PointToolIcon = wrapLucide(CircleDot);
export const RectToolIcon = wrapLucide(RectangleHorizontal);
export const ExplodeIcon = wrapLucide(Blocks);
export const BrushToolIcon = wrapLucide(Brush);
export function MosaicToolIcon({ size = 16 }: Pick<AppIconProps, 'size'>) {
  return (
    <span
      aria-hidden="true"
      style={{
        display: 'inline-block',
        flex: 'none',
        width: size,
        height: size,
        backgroundColor: 'currentColor',
        mask: `url("${mosaicMask}") center / contain no-repeat`,
      }}
    />
  );
}
export const CropToolIcon = wrapLucide(Crop);
export const EditIcon = wrapLucide(Pencil);
export const NoteAddIcon = wrapLucide(MessageSquarePlus);
export const MagnifierIcon = wrapLucide(Search);
export const SearchIcon = wrapLucide(Search);
export const VideoIcon = wrapLucide(Video);
export const FitCanvasIcon = wrapLucide(Scaling);
export const PlayIcon = wrapLucide(Play);
export const PauseIcon = wrapLucide(Pause);
export const StopIcon = wrapLucide(Square);

// History, Navigation, Status & Settings
export const UndoIcon = wrapLucide(Undo2);
export const RedoIcon = wrapLucide(Redo2);
export const ResetIcon = wrapLucide(RotateCcw);
export const SyncIcon = wrapLucide(RefreshCw);
export const DeleteIcon = wrapLucide(Trash2);
export const ShowIcon = wrapLucide(Eye);
export const HideIcon = wrapLucide(EyeOff);
export const InspectorOpenIcon = wrapLucide(PanelRightOpen);
export const InspectorCloseIcon = wrapLucide(PanelRightClose);
export const SettingsIcon = wrapLucide(Settings);
export const ConfigSlidersIcon = wrapLucide(SlidersHorizontal);
export const AiSparkleIcon = wrapLucide(Sparkles);
export const SendIcon = wrapLucide(Send);
export const MoreIcon = wrapLucide(MoreHorizontal);
export const CloseIcon = wrapLucide(X);
export const CheckIcon = wrapLucide(Check);
export const PlusIcon = wrapLucide(Plus);
export const MinusIcon = wrapLucide(Minus);
export const BackIcon = wrapLucide(ArrowLeft);
export const ExternalLinkIcon = wrapLucide(ArrowUpRight);
export const ChevronDownIcon = wrapLucide(ChevronDown);
export const SunIcon = wrapLucide(Sun);
export const MoonIcon = wrapLucide(Moon);
export const StarIcon = wrapLucide(Star);
export const SpinnerIcon = wrapLucide(LoaderCircle);
export const ThemeIcon = wrapLucide(Palette);
export const LanguageIcon = wrapLucide(Globe);
export const PowerIcon = wrapLucide(Power);
export const ConfirmDiscardIcon = wrapLucide(ShieldAlert);
export const KeyboardIcon = wrapLucide(Keyboard);
export const WindowTitleIcon = wrapLucide(AppWindow);
export const ScrollAxisIcon = wrapLucide(MoveVertical);
export const TerminalIcon = wrapLucide(Terminal);
export const ShieldOkIcon = wrapLucide(ShieldCheck);
export const StatusOkIcon = wrapLucide(CheckCircle2);
export const ClickActionIcon = wrapLucide(MousePointerClick);
export const DocumentIcon = wrapLucide(FileText);
