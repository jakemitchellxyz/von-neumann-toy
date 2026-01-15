#!/bin/bash

# Downloads NASA Blue Marble Next Generation topography images for all 12 months
# Each month has 8 tiles: 4 areas (A-D) × 2 hemispheres (1-2)

BASE_URL="https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography"
OUTPUT_DIR="./defaults/earth-surface/blue-marble"

MONTHS=("january" "february" "march" "april" "may" "june" 
        "july" "august" "september" "october" "november" "december")
AREAS=("A" "B" "C" "D")
HEMISPHERES=("1" "2")

mkdir -p "$OUTPUT_DIR"

for month_idx in "${!MONTHS[@]}"; do
    month_name="${MONTHS[$month_idx]}"
    month_num=$(printf "%02d" $((month_idx + 1)))
    
    for area in "${AREAS[@]}"; do
        for hemi in "${HEMISPHERES[@]}"; do
            filename="world.topo.2004${month_num}.3x21600x21600.${area}${hemi}.jpg"
            filepath="${OUTPUT_DIR}/${filename}"
            url="${BASE_URL}/${month_name}/${filename}"
            
            if [[ -f "$filepath" ]]; then
                echo "Skipping ${filename} (already exists)"
            else
                echo "Downloading ${filename}..."
                curl -fSL "$url" -o "$filepath"
            fi
        done
    done
done

echo "Done!"
