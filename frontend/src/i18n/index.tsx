import { createContext, useContext, useLayoutEffect, useMemo, type ReactNode } from 'react';
import type { GeneralPreferences } from '../bridge/types';
import zhCN, { type MessageCatalog, type MessageKey } from './zh_CN';
import zhTW from './zh_TW';
import en from './en';

export const LANGUAGE_LABEL_KEYS = {
  system: 'settings.followSystem',
  zh_CN: 'settings.simplifiedChinese',
  zh_TW: 'settings.traditionalChinese',
  en: 'settings.english',
} as const;

export type Locale = NonNullable<GeneralPreferences['effectiveLanguage']>;
type Value = string | number;
type Placeholders<Message extends string> =
  Message extends `${string}{${infer Name}}${infer Tail}` ? Name | Placeholders<Tail> : never;
type MessageArgs<Key extends MessageKey> =
  [Placeholders<(typeof zhCN)[Key]>] extends [never]
    ? []
    : [values: Record<Placeholders<(typeof zhCN)[Key]>, Value>];

export type Translate = <Key extends MessageKey>(key: Key, ...args: MessageArgs<Key>) => string;

const catalogs: Record<Locale, MessageCatalog> = { zh_CN: zhCN, zh_TW: zhTW, en };
const documentLanguages: Record<Locale, string> = { zh_CN: 'zh-CN', zh_TW: 'zh-TW', en: 'en' };

export function resolveLocale(
  language: GeneralPreferences['language'],
  effectiveLanguage: GeneralPreferences['effectiveLanguage'],
  browserLanguage: string,
): Locale {
  if (language !== 'system') return language;
  if (effectiveLanguage) return effectiveLanguage;
  const parts = browserLanguage.toLowerCase().split(/[-_]/);
  if (parts[0] !== 'zh') return 'en';
  return parts.some((part) => ['hant', 'tw', 'hk', 'mo'].includes(part)) ? 'zh_TW' : 'zh_CN';
}

export function createTranslator(locale: Locale): Translate {
  return (key, ...args) => {
    const values: Record<string, Value> | undefined = args[0];
    return catalogs[locale][key].replace(/\{(\w+)\}/g, (placeholder, name: string) =>
      values?.[name] === undefined ? placeholder : String(values[name]),
    );
  };
}

const I18nContext = createContext<Translate>(createTranslator('en'));

export function I18nProvider({ locale, viewTitle, children }: {
  locale: Locale;
  viewTitle: string;
  children: ReactNode;
}) {
  const t = useMemo(() => createTranslator(locale), [locale]);

  useLayoutEffect(() => {
    document.documentElement.lang = documentLanguages[locale];
    document.title = `${viewTitle} · EditHere`;
  }, [locale, viewTitle]);

  return <I18nContext.Provider value={t}>{children}</I18nContext.Provider>;
}

export function useI18n(): Translate {
  return useContext(I18nContext);
}
