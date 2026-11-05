<script lang="ts">
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import { onMount } from 'svelte';
	import Clock from '~icons/tabler/clock';
	import DeviceMobile from '~icons/tabler/device-mobile';

	import type { Relogio } from '$lib/types/models';
	import { getRelogio, salvarRelogio } from '$lib/services/services';

	let relogio = $state<Relogio>({
		datetime: ''
	});

	let salvando = $state(false);
	let carregando = $state(true);

	onMount(() => {
		getRelogio()
			.then(data => {
				if (data) {
					relogio = { datetime: data.datetime || '' };
					notifications.success('Relógio carregado com sucesso!', 3000);
				}
			})
			.catch(error => {
				console.error('Erro ao carregar o relógio:', error);
				notifications.error('Erro ao carregar o relógio. Tente novamente.', 3000);
			})
			.finally(() => {
				carregando = false;
			});
	});

	function sincronizarComNavegador() {
		const agora = new Date();
		// Ajusta para o fuso local do navegador no formato ISO local: YYYY-MM-DDTHH:mm:ss
		const offsetMs = agora.getTimezoneOffset() * 60000;
		const localIso = new Date(agora.getTime() - offsetMs).toISOString().slice(0, 19);
		relogio.datetime = localIso;
		notifications.info('Horário preenchido com a data/hora deste dispositivo. Clique em "Salvar Horário" para gravar no RTC.', 4000);
	}

	async function salvar() {
		if (salvando) return;
		if (!relogio.datetime) {
			notifications.error('Por favor, informe uma data e hora válidas.', 3000);
			return;
		}

		// Garante formato %Y-%m-%dT%H:%M:%S exigido pelo ESP32 caso o browser omita segundos
		let dt = relogio.datetime;
		if (dt.length === 16) {
			dt = `${dt}:00`;
		}

		salvando = true;
		try {
			const relogioData = await salvarRelogio({ datetime: dt });
			if (relogioData) {
				relogio = { ...relogioData };
				notifications.success('Relógio salvo com sucesso!', 3000);
			} else {
				notifications.error('Erro ao salvar o relógio. Verifique as permissões.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar o relógio:', error);
			notifications.error('Erro ao salvar o relógio. Tente novamente.', 3000);
		} finally {
			salvando = false;
		}
	}
</script>

<SettingsCard collapsible={false} maxWidth="max-w-3xl">
	{#snippet icon()}
		<Clock class="mr-2 inline h-6 w-6 text-primary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Relógio RTC (DS3231)</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full">
			<div class="flex flex-col sm:flex-row sm:items-center justify-between pb-3 mb-4 border-b border-base-200 gap-2">
				<div>
					<h3 class="text-base font-bold text-base-content">Relógio Oficial da Tomada de Tempos</h3>
					<p id="datetime-help" class="text-xs text-base-content/60">
						Data e hora física do chip RTC DS3231 usada para registrar largadas e chegadas.
					</p>
				</div>
				<div>
					<span class="badge badge-success badge-sm text-white">DS3231 I2C</span>
				</div>
			</div>

			<label for="datetime" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
				Data e Hora Atual (com segundos)
			</label>
			<input
				id="datetime"
				name="datetime"
				type="datetime-local"
				step="1"
				bind:value={relogio.datetime}
				disabled={carregando || salvando}
				aria-describedby="datetime-help"
				class="input input-bordered input-sm sm:input-md w-full mb-6 font-mono text-sm"
			/>

			<div class="flex flex-col sm:flex-row items-stretch sm:items-center gap-2.5 pt-2 border-t border-base-200">
				<button
					type="button"
					onclick={salvar}
					disabled={carregando || salvando}
					class="btn btn-primary btn-sm sm:btn-md gap-2"
				>
					{#if salvando}
						<span class="loading loading-spinner loading-sm"></span>
						<span>Salvando...</span>
					{:else}
						<span>Salvar Horário no RTC</span>
					{/if}
				</button>

				<button
					type="button"
					onclick={sincronizarComNavegador}
					disabled={carregando || salvando}
					class="btn btn-outline btn-sm sm:btn-md gap-2"
				>
					<DeviceMobile class="h-4 w-4" />
					<span>Usar Horário deste Dispositivo</span>
				</button>
			</div>
		</div>
	</div>
</SettingsCard>
